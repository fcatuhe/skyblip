// What a unit wrote about its last install, read over the link (#278, retired with #294).
#include <string>

#include "core/dfu/install_report.h"
#include "doctest/doctest.h"
#include "test/support/product_rig.h"
#include "test/support/update_rig.h"

using namespace skyblip;

namespace {

std::string report_frame(Rig& rig, uint32_t now_ms = 0) {
    rig.send("{\"command\":\"install_report\"}");
    rig.run(now_ms, now_ms + 200);
    return rig.last_on(events::Endpoint::Config);
}

bool holds_a_report(Rig& rig) {
    uint8_t blob[dfu::kInstallReportBytes];
    size_t count = 0;
    return rig.platform.key_value().read("install.report", blob, sizeof(blob), count) == Status::Ok;
}

bool says(const std::string& frame, const char* pair) {
    return frame.find(pair) != std::string::npos;
}

struct OnProbation {
    Rig before;
    std::optional<Rebooted> after;

    OnProbation() {
        stage_versions(before);
        REQUIRE(before.setup() == Status::Ok);
        install_and_swap(before);
        after.emplace(before, kStaged, /*confirmed=*/false);
        after->rig.platform.system_power().causes = power::ResetCause::Software;
    }

    Rig& rig() { return after->rig; }
};

}  // namespace

TEST_CASE("install report: a damaged slot 1 is never marked, and the unit comes back to say so") {
    Rig before;
    stage_versions(before);
    before.platform.dfu().swap_request = {ports::StagedCheck::Damaged, 0, ports::SwapType::None};
    REQUIRE(before.setup() == Status::Ok);
    install_and_swap(before);
    CHECK(before.platform.dfu().swap_requests == 1);
    CHECK(before.platform.dfu().triggered == 1);

    Rebooted after(before, kRunning, /*confirmed=*/true, kStaged);
    after.rig.platform.system_power().causes = power::ResetCause::Software;
    REQUIRE(after.rig.setup() == Status::Ok);
    const std::string frame = report_frame(after.rig);
    CHECK(says(frame, "\"check\":\"damaged\""));
    CHECK(says(frame, "\"swap\":\"none\""));
    CHECK(says(frame, "\"request_error\":0"));
    CHECK(says(frame, "\"boots\":1,\"probation_boots\":0,\"proof\":\"none\""));
    CHECK(says(frame, "\"reset\":\"software\""));
}

TEST_CASE("install report: a request the trailer refused is kept with its error") {
    Rig before;
    stage_versions(before);
    before.platform.dfu().swap_request = {ports::StagedCheck::Matched, -14, ports::SwapType::None};
    REQUIRE(before.setup() == Status::Ok);
    install_and_swap(before);

    Rebooted after(before, kRunning, /*confirmed=*/true, kStaged);
    REQUIRE(after.rig.setup() == Status::Ok);
    const std::string frame = report_frame(after.rig);
    CHECK(says(frame, "\"check\":\"matched\""));
    CHECK(says(frame, "\"request_error\":-14"));
}

TEST_CASE("install report: a new image that ran and was reverted says how far it got") {
    OnProbation probation;
    REQUIRE(probation.rig().setup() == Status::Ok);
    probation.rig().run(0, 5000);
    CHECK(probation.rig().platform.dfu().confirms == 0);

    Rebooted reverted(probation.rig(), kRunning, /*confirmed=*/true, kStaged);
    reverted.rig.platform.system_power().causes = power::ResetCause::Watchdog;
    REQUIRE(reverted.rig.setup() == Status::Ok);
    CHECK(config(reverted.rig).image_state() == dfu::ImageState::Reverted);
    const std::string frame = report_frame(reverted.rig);
    CHECK(says(frame, "\"check\":\"matched\",\"swap\":\"test\""));
    CHECK(says(frame, "\"boots\":2,\"probation_boots\":1,\"proof\":\"started\""));
    CHECK(says(frame, "\"reset\":\"watchdog\""));
}

TEST_CASE("install report: a confirmation the trailer refuses is the last proof on record") {
    OnProbation probation;
    probation.rig().platform.dfu().confirm_fails = true;
    REQUIRE(probation.rig().setup() == Status::Ok);
    receiver_speaks_without_a_fix(probation.rig());
    probation.rig().run(0, 5000);
    CHECK(says(report_frame(probation.rig(), 5000), "\"proof\":\"confirm_failed\""));
}

TEST_CASE("install report: the image that confirms itself forgets the report with the record") {
    OnProbation probation;
    REQUIRE(probation.rig().setup() == Status::Ok);
    CHECK(says(report_frame(probation.rig()), "\"probation_boots\":1"));

    receiver_speaks_without_a_fix(probation.rig());
    probation.rig().run(200, 5000);
    REQUIRE(config(probation.rig()).image_state() == dfu::ImageState::Confirmed);
    CHECK(report_frame(probation.rig(), 5000) == "{\"command\":\"install_report\"}");
    CHECK_FALSE(holds_a_report(probation.rig()));
}

// An older image forgets the record and leaves the report, which the next boot cleans up.
TEST_CASE("install report: a report with no update record beside it is erased at boot") {
    Rig rig;
    uint8_t blob[dfu::kInstallReportBytes];
    const size_t bytes = dfu::to_blob(dfu::InstallReport{}, blob, sizeof(blob));
    REQUIRE(rig.platform.key_value().write("install.report", blob, bytes) == Status::Ok);

    REQUIRE(rig.setup() == Status::Ok);
    CHECK_FALSE(holds_a_report(rig));
    CHECK(report_frame(rig) == "{\"command\":\"install_report\"}");
}
