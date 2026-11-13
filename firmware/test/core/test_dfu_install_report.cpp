// The install report's arithmetic and its blob (#278, retired with #294).
#include "core/dfu/install_report.h"
#include "doctest/doctest.h"

using namespace skyblip;

TEST_CASE("install report: a probation boot starts a new run, a boot of the old image does not") {
    dfu::InstallReport report{};
    report.booted(/*on_probation=*/true, power::ResetReason::Software);
    CHECK(report.proved(dfu::Proof::GnssHeard));
    CHECK_FALSE(report.proved(dfu::Proof::Started));
    CHECK(report.proof == dfu::Proof::GnssHeard);

    report.booted(/*on_probation=*/false, power::ResetReason::Lockup);
    CHECK(report.boots == 2);
    CHECK(report.probation_boots == 1);
    CHECK(report.proof == dfu::Proof::GnssHeard);
    CHECK(report.reset == power::ResetReason::Lockup);
}

TEST_CASE("install report: the boot counts stop at 255 rather than wrap to none") {
    dfu::InstallReport report{};
    for (int boot = 0; boot < 300; boot++) report.booted(true, power::ResetReason::Watchdog);
    CHECK(report.boots == 255);
    CHECK(report.probation_boots == 255);
}

TEST_CASE("install report: the blob reads back what was written, and refuses another layout") {
    dfu::InstallReport written{};
    written.request = {ports::StagedCheck::Matched, -5, ports::SwapType::Test};
    written.booted(true, power::ResetReason::Watchdog);
    (void)written.proved(dfu::Proof::ConfirmFailed);
    uint8_t blob[dfu::kInstallReportBytes];
    REQUIRE(dfu::to_blob(written, blob, sizeof(blob)) == dfu::kInstallReportBytes);
    CHECK(dfu::to_blob(written, blob, sizeof(blob) - 1) == 0);

    dfu::InstallReport read{};
    REQUIRE(dfu::from_blob(blob, sizeof(blob), read));
    CHECK(read.request.check == ports::StagedCheck::Matched);
    CHECK(read.request.error == -5);
    CHECK(read.request.swap == ports::SwapType::Test);
    CHECK(read.boots == 1);
    CHECK(read.probation_boots == 1);
    CHECK(read.proof == dfu::Proof::ConfirmFailed);
    CHECK(read.reset == power::ResetReason::Watchdog);

    blob[0] = 2;
    CHECK_FALSE(dfu::from_blob(blob, sizeof(blob), read));
    CHECK_FALSE(dfu::from_blob(blob, sizeof(blob) - 1, read));
}
