// The whole product taking an update; the bootloader is the one thing the host cannot run.
#include <string>

#include "doctest/doctest.h"
#include "products/skyblip_go/pages/installing.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

constexpr ports::ImageVersion kRunning{0, 1, 0, 12};
constexpr ports::ImageVersion kStaged{0, 2, 0, 15};

void on_ground(Rig& rig, uint32_t& t) {
    rig.push_fix(/*alt_m=*/0, /*updates=*/1);
    rig.run(t, t + 200);
    t += 200;
}

void receiver_speaks_without_a_fix(Rig& rig) {
    gnss::GnssSolution f{};
    f.fix_valid = false;
    f.updates = 1;
    rig.product.bus().gnss.push(f);
}

int length(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

bool glass_reads(const ui::Canvas& fb, int x, int y, const char* text, int scale) {
    go::Glass expected;
    expected.clear(true);
    expected.draw_text(x, y, text, true, scale);
    for (int dy = 0; dy < 7 * scale; dy++)
        for (int dx = 0; dx < length(text) * go::kInstallingCellW * scale; dx++)
            if (fb.get_pixel(x + dx, y + dy) != expected.get_pixel(x + dx, y + dy)) return false;
    return true;
}

comms::ConfigService& config(Rig& rig) { return rig.product.config().config(); }

void pass(Rig& rig, uint32_t& t) {
    t += 50;
    rig.run(t, t);
}

void open_upload_window(Rig& rig, uint32_t& t) {
    on_ground(rig, t);
    rig.send("{\"cmd\":\"dfu\"}");
    rig.run(t, t + 200);
    t += 200;
    config(rig).confirm();
}

void climb_until_the_link_sees_it(Rig& rig, uint32_t& t) {
    for (int second = 0; second < 20; second++) {
        gnss::GnssSolution climbing{};
        climbing.fix_valid = true;
        climbing.speed_mm_s = 50000;
        climbing.alt_msl_mm = 1200 * 1000;
        climbing.updates = 1;
        rig.product.bus().gnss.push(climbing);
        for (int i = 0; i < 10; i++) {
            pass(rig, t);
            if (config(rig).flight_state() == flight::FlightState::Airborne) return;
        }
    }
}

void stage_versions(Rig& rig) {
    rig.platform.dfu().has_running = true;
    rig.platform.dfu().running = kRunning;
    rig.platform.dfu().has_staged = true;
    rig.platform.dfu().staged = kStaged;
    rig.platform.dfu().finished_upload = true;
}

void apply_and_swap(Rig& rig) {
    uint32_t t = 0;
    on_ground(rig, t);
    rig.send("{\"cmd\":\"apply\"}");
    rig.run(t, t + 200);
    t += 200;
    config(rig).confirm();
    rig.run(t, t + power::kParkMs + power::kReleaseSettleMs + 500);
}

// The unsolicited frame a phone gets on connecting, wherever the status push landed.
std::string update_frame(Rig& rig) {
    for (const auto& f : rig.platform.link().sent)
        if (f.bytes.find("\"cmd\":\"update\"") != std::string::npos) return f.bytes;
    return "";
}

bool attempt_recorded(Rig& rig) {
    uint8_t blob[dfu::kUpdateRecordBytes];
    size_t n = 0;
    return rig.platform.kv().read("update", blob, sizeof(blob), n) == Status::Ok;
}

// The device after the bootloader has run: same flash, a fresh boot.
struct Rebooted {
    Rig rig;
    Rebooted(Rig& before, ports::ImageVersion running, bool confirmed) {
        rig.platform.kv() = before.platform.kv();
        rig.platform.dfu().has_running = true;
        rig.platform.dfu().running = running;
        rig.platform.dfu().image_confirmed = confirmed;
    }
};

}  // namespace

TEST_CASE("product: a fresh image confirms itself once the receiver has spoken, fix or no fix") {
    Rig rig;
    rig.platform.dfu().image_confirmed = false;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(config(rig).image_state() == dfu::ImageState::Probation);

    // Indoors: radio up, panel drawn, nothing off the UART yet
    rig.run(0, 30000);
    CHECK(rig.platform.dfu().confirms == 0);
    CHECK(rig.state().panel_presented);

    receiver_speaks_without_a_fix(rig);
    rig.run(30000, 30500);
    CHECK(rig.platform.dfu().confirms == 1);
    CHECK(rig.platform.dfu().confirmed());
    CHECK(config(rig).image_state() == dfu::ImageState::Confirmed);

    rig.run(30500, 60000);
    CHECK(rig.platform.dfu().confirms == 1);
}

TEST_CASE("product: a receiver that never speaks keeps the image on probation") {
    Rig rig;
    rig.platform.dfu().image_confirmed = false;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 120000);
    CHECK(rig.platform.dfu().confirms == 0);
    CHECK(config(rig).image_state() == dfu::ImageState::Probation);
}

TEST_CASE("product: an image that was installed confirmed is never confirmed again") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    on_ground(rig, t);
    rig.run(t, t + 5000);
    CHECK(rig.platform.dfu().confirms == 0);
    CHECK(config(rig).image_state() == dfu::ImageState::Confirmed);
}

TEST_CASE("product: a board with no panel does not wait for one to confirm") {
    constexpr ports::Capabilities kNoPanel = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(ports::Capability::Display));
    Rig rig(kNoPanel);
    rig.platform.dfu().image_confirmed = false;
    REQUIRE(rig.setup() == Status::Ok);
    receiver_speaks_without_a_fix(rig);
    rig.run(0, 500);
    CHECK_FALSE(rig.state().panel_presented);
    CHECK(rig.platform.dfu().confirms == 1);
}

TEST_CASE("product: a confirmed apply parks the device and paints the glass before the swap") {
    Rig rig;
    stage_versions(rig);
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    on_ground(rig, t);
    rig.run(t, t + 1000);
    t += 1000;

    rig.send("{\"cmd\":\"apply\"}");
    rig.run(t, t + 200);
    t += 200;
    REQUIRE(config(rig).pending() == comms::Pending::Apply);
    CHECK(rig.platform.dfu().triggered == 0);

    config(rig).confirm();
    rig.run(t, t + 200);
    t += 200;
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::Install);
    CHECK(rig.product.shutdown().phase() == power::ShutdownPhase::Parking);
    CHECK(rig.product.installing());
    CHECK(rig.product.board().rf().sleeps() == 1);
    CHECK_FALSE(rig.product.screen().powered());
    CHECK_FALSE(config(rig).install_requested());

    // the panel is still clocking its refresh, and nothing is on record yet
    CHECK(rig.platform.dfu().triggered == 0);
    CHECK_FALSE(attempt_recorded(rig));

    rig.run(t, t + power::kParkMs + power::kReleaseSettleMs + 5000);
    CHECK(glass_reads(rig.platform.chips().epd.framebuffer(), go::kInstallingLeftX,
                      go::kInstallingTitleY, go::kInstallingTitle, 2));
    CHECK(rig.platform.dfu().triggered == 1);
    CHECK_FALSE(rig.product.ready_to_power_off());
    CHECK(attempt_recorded(rig));

    uint8_t blob[dfu::kUpdateRecordBytes];
    size_t n = 0;
    REQUIRE(rig.platform.kv().read("update", blob, sizeof(blob), n) == Status::Ok);
    dfu::UpdateRecord record;
    REQUIRE(dfu::from_blob(blob, n, record));
    CHECK(record.from == kRunning);
    CHECK(record.to == kStaged);
}

TEST_CASE("product: the SMP hook's gate opens on the pass after the window is confirmed") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    open_upload_window(rig, t);
    REQUIRE(config(rig).upload_allowed());
    CHECK_FALSE(rig.platform.dfu().upload_allowed_published);

    pass(rig, t);
    CHECK(rig.platform.dfu().upload_allowed_published);
}

// A chunk the MCUmgr work queue reads against a stale gate is a chunk written in flight.
TEST_CASE("product: the pass that learns of the take-off closes the SMP hook's gate in that pass") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    open_upload_window(rig, t);
    pass(rig, t);
    REQUIRE(rig.platform.dfu().upload_allowed_published);

    climb_until_the_link_sees_it(rig, t);
    REQUIRE(config(rig).flight_state() == flight::FlightState::Airborne);
    CHECK_FALSE(rig.platform.dfu().upload_allowed_published);
}

TEST_CASE("product: apply is refused below the low-battery warning and nothing parks") {
    Rig rig;
    stage_versions(rig);
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    on_ground(rig, t);
    rig.platform.battery().millivolts = 3400;
    rig.run(t, t + 8000);
    t += 8000;
    REQUIRE(rig.state().power.level == power::PowerLevel::Low);

    rig.send("{\"cmd\":\"apply\"}");
    rig.run(t, t + 200);
    t += 200;
    CHECK(config(rig).pending() == comms::Pending::None);
    CHECK(rig.last_on(events::Endpoint::Config).find("low_power") != std::string::npos);
    CHECK_FALSE(rig.product.shutdown().going_down());
    CHECK(rig.platform.dfu().triggered == 0);
}

TEST_CASE(
    "product: the image that boots after a revert tells the phone which update did not take") {
    Rig before;
    stage_versions(before);
    REQUIRE(before.setup() == Status::Ok);
    apply_and_swap(before);
    REQUIRE(before.platform.dfu().triggered == 1);

    Rebooted after(before, kRunning, /*confirmed=*/true);
    REQUIRE(after.rig.setup() == Status::Ok);
    CHECK(config(after.rig).image_state() == dfu::ImageState::Reverted);

    after.rig.raise_link();
    after.rig.run(0, 200);
    const std::string frame = update_frame(after.rig);
    CHECK(frame.find("\"image\":\"reverted\"") != std::string::npos);
    CHECK(frame.find("\"from\":\"0.1.0+12\"") != std::string::npos);
    CHECK(frame.find("\"to\":\"0.2.0+15\"") != std::string::npos);

    Rebooted later(after.rig, kRunning, /*confirmed=*/true);
    REQUIRE(later.rig.setup() == Status::Ok);
    CHECK(config(later.rig).image_state() == dfu::ImageState::Reverted);
}

TEST_CASE("product: the image that lands forgets the attempt once it has confirmed itself") {
    Rig before;
    stage_versions(before);
    REQUIRE(before.setup() == Status::Ok);
    apply_and_swap(before);
    REQUIRE(before.platform.dfu().triggered == 1);

    Rebooted after(before, kStaged, /*confirmed=*/false);
    REQUIRE(after.rig.setup() == Status::Ok);
    CHECK(config(after.rig).image_state() == dfu::ImageState::Probation);
    CHECK(attempt_recorded(after.rig));

    receiver_speaks_without_a_fix(after.rig);
    after.rig.run(0, 5000);
    CHECK(after.rig.platform.dfu().confirms == 1);
    CHECK(config(after.rig).image_state() == dfu::ImageState::Confirmed);
    CHECK_FALSE(attempt_recorded(after.rig));
}

TEST_CASE("product: a trailer that will not take the confirmation leaves the image on probation") {
    Rig rig;
    rig.platform.dfu().image_confirmed = false;
    rig.platform.dfu().confirm_fails = true;
    REQUIRE(rig.setup() == Status::Ok);
    receiver_speaks_without_a_fix(rig);
    rig.run(0, 10000);
    CHECK(rig.platform.dfu().confirms == 3);
    CHECK_FALSE(rig.platform.dfu().confirmed());
    CHECK(config(rig).image_state() == dfu::ImageState::Probation);
}

// The finished upload is held in RAM, so a restart costs the pilot the upload and not a boot.
TEST_CASE("product: an image staged before a restart is refused as unfinished, not swapped into") {
    Rig before;
    stage_versions(before);
    REQUIRE(before.setup() == Status::Ok);

    Rebooted after(before, kRunning, /*confirmed=*/true);
    after.rig.platform.dfu().has_staged = true;
    after.rig.platform.dfu().staged = kStaged;
    REQUIRE(after.rig.setup() == Status::Ok);
    uint32_t t = 0;
    on_ground(after.rig, t);
    after.rig.send("{\"cmd\":\"apply\"}");
    after.rig.run(t, t + 200);
    CHECK(config(after.rig).pending() == comms::Pending::None);
    CHECK(after.rig.last_on(events::Endpoint::Config).find("upload_unfinished") !=
          std::string::npos);
    CHECK(after.rig.platform.dfu().triggered == 0);
}

TEST_CASE("product: an image nobody staged over the air clears a stale attempt") {
    Rig before;
    stage_versions(before);
    REQUIRE(before.setup() == Status::Ok);
    apply_and_swap(before);
    REQUIRE(attempt_recorded(before));

    // a .uf2 dropped on the bootloader volume is neither side of the attempt
    Rebooted flashed(before, ports::ImageVersion{0, 3, 0, 1}, /*confirmed=*/true);
    REQUIRE(flashed.rig.setup() == Status::Ok);
    CHECK(config(flashed.rig).image_state() == dfu::ImageState::Confirmed);
    CHECK_FALSE(attempt_recorded(flashed.rig));
}
