// The whole product taking an update; the bootloader is the one thing the host cannot run.
#include <cstring>
#include <string>

#include "core/settings/blob.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/boot.h"
#include "products/skyblip_go/pages/installing.h"
#include "products/skyblip_go/pages/recovery.h"
#include "runtime/tasks.h"
#include "test/support/product_rig.h"
#include "test/support/update_rig.h"

using namespace skyblip;

namespace {

constexpr ports::ImageVersion kRunning{0, 1, 0, 12};
constexpr ports::ImageVersion kStaged{0, 2, 0, 15};

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

void pass(Rig& rig, uint32_t& t) {
    t += 50;
    rig.run(t, t);
}

void open_upload_window(Rig& rig, uint32_t& t) {
    on_ground(rig, t);
    rig.send("{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}");
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
}

void land_upload(Rig& rig, uint32_t& t) {
    rig.platform.dfu().finished_upload = true;
    pass(rig, t);
}

void install_and_swap(Rig& rig) {
    uint32_t t = 0;
    open_upload_window(rig, t);
    land_upload(rig, t);
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

const go::BootPart& storage_row(Rig& rig) {
    for (int i = 0; i < go::kBootPartCount; i++)
        if (std::string(rig.product.boot_rows()[i].name) == "STORAGE")
            return rig.product.boot_rows()[i];
    return rig.product.boot_rows()[0];
}

// What a later image stores: one layout ahead of this one, sealed the way every blob is.
struct NewerBlob {
    uint8_t bytes[settings::blob_bytes(40)]{};
    NewerBlob() {
        uint8_t payload[40];
        for (size_t i = 0; i < sizeof(payload); i++) payload[i] = static_cast<uint8_t>(0xA0 + i);
        settings::seal(go::kBlobVersion + 1, payload, sizeof(payload), bytes, sizeof(bytes));
    }
};

bool holds(Rig& rig, const char* key, const uint8_t* blob, size_t len) {
    uint8_t stored[64];
    size_t n = 0;
    if (rig.platform.kv().read(key, stored, sizeof(stored), n) != Status::Ok) return false;
    return n == len && std::memcmp(stored, blob, len) == 0;
}

void set_callsign_over_the_link(Rig& rig, uint32_t& t, const char* callsign) {
    on_ground(rig, t);
    std::string json = "{\"cmd\":\"set\",\"callsign\":\"";
    json += callsign;
    json += "\"}";
    rig.send(json.c_str());
    rig.run(t, t + 200);
    t += 200;
    config(rig).confirm();
    rig.run(t, t + 3000);
    t += 3000;
}

}  // namespace

TEST_CASE("product: an upload that lands parks the device and paints the glass before the swap") {
    Rig rig;
    stage_versions(rig);
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    open_upload_window(rig, t);
    rig.run(t, t + 3000);
    t += 3000;
    CHECK(glass_reads(rig.platform.chips().epd.framebuffer(), go::kInstallingLeftX,
                      go::kInstallingTitleY, go::kReceivingTitle, 2));
    CHECK(rig.platform.dfu().triggered == 0);

    land_upload(rig, t);
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

TEST_CASE("product: with no watchdog running a recovery paints the bootloader page, then reboots") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    on_ground(rig, t);

    rig.send("{\"cmd\":\"recovery\"}");
    rig.run(t, t + 200);
    t += 200;
    REQUIRE(config(rig).pending() == comms::Pending::Recovery);

    config(rig).confirm();
    rig.run(t, t + 200);
    t += 200;
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::Recovery);
    CHECK(rig.product.board().rf().sleeps() == 1);
    CHECK(rig.platform.dfu().recoveries == 0);

    rig.run(t, t + power::kParkMs + power::kReleaseSettleMs + 5000);
    const ui::Canvas& glass = rig.platform.chips().epd.framebuffer();
    CHECK(glass_reads(glass, go::kInstallingLeftX, go::kInstallingTitleY, go::kRecoveryTitle, 2));
    CHECK(glass_reads(glass, go::kInstallingLeftX, go::installing_body_y(0), go::kRecoveryRunning,
                      1));
    CHECK(rig.platform.dfu().recoveries == 1);
    CHECK_FALSE(rig.product.ready_to_power_off());
}

TEST_CASE("product: under a running watchdog a recovery asks for the press, then drops the rails") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    REQUIRE(rig.platform.watchdog().arm(runtime::kHardwareWatchdogMs) == Status::Ok);
    uint32_t t = 0;
    on_ground(rig, t);

    rig.send("{\"cmd\":\"recovery\"}");
    rig.run(t, t + 200);
    t += 200;
    config(rig).confirm();
    rig.run(t, t + 200);
    t += 200;
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::Recovery);
    CHECK_FALSE(rig.product.ready_to_power_off());
    CHECK(rig.platform.dfu().recoveries == 0);

    rig.run(t, t + power::kParkMs + power::kReleaseSettleMs + 5000);
    const ui::Canvas& glass = rig.platform.chips().epd.framebuffer();
    CHECK(glass_reads(glass, go::kInstallingLeftX, go::kInstallingTitleY, go::kRecoveryTitle, 2));
    CHECK(glass_reads(glass, go::kInstallingLeftX, go::installing_body_y(0),
                      go::kRecoveryAwaitsPress, 1));
    CHECK(rig.platform.dfu().recoveries == 1);
    CHECK(rig.product.ready_to_power_off());
    CHECK(std::string(power::to_string(rig.product.shutdown().reason())) == "RECOVERY");
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

TEST_CASE("product: a single press on the receiving page closes the window, and nothing installs") {
    Rig rig;
    stage_versions(rig);
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    open_upload_window(rig, t);
    rig.run(t, t + 3000);
    t += 3000;
    REQUIRE(config(rig).receiving_firmware());

    rig.press(t);
    rig.run(t, t + 3000);
    t += 3000;
    CHECK_FALSE(config(rig).receiving_firmware());
    CHECK(rig.last_on(events::Endpoint::Config).find("cancelled") != std::string::npos);
    CHECK_FALSE(glass_reads(rig.platform.chips().epd.framebuffer(), go::kInstallingLeftX,
                            go::kInstallingTitleY, go::kReceivingTitle, 2));

    land_upload(rig, t);
    rig.run(t, t + 200);
    CHECK_FALSE(rig.product.shutdown().going_down());
    CHECK(rig.platform.dfu().triggered == 0);
}

TEST_CASE("product: an upload that lands on a critical cell is refused and nothing parks") {
    Rig rig;
    stage_versions(rig);
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    open_upload_window(rig, t);
    rig.platform.battery().millivolts = 3400;
    rig.run(t, t + 8000);
    t += 8000;
    REQUIRE(rig.state().power.level == power::PowerLevel::Critical);

    land_upload(rig, t);
    rig.run(t, t + 200);
    CHECK(rig.last_on(events::Endpoint::Config).find("low_power") != std::string::npos);
    CHECK_FALSE(rig.product.shutdown().going_down());
    CHECK(rig.platform.dfu().triggered == 0);
}

TEST_CASE(
    "product: the image that boots after a revert tells the phone which update did not take") {
    Rig before;
    stage_versions(before);
    REQUIRE(before.setup() == Status::Ok);
    install_and_swap(before);
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

// The full image of the running version, sent to give a fitted hub its image: by version it landed.
TEST_CASE("product: a same-version image the bootloader reverted reads as reverted by its hash") {
    constexpr ports::ImageHash kSlim{{0x51, 0x1A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}};
    constexpr ports::ImageHash kFull{{0xF0, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02}};
    Rig before;
    stage_versions(before);
    before.platform.dfu().staged = kRunning;
    before.platform.dfu().has_running_hash = true;
    before.platform.dfu().running_image_hash = kSlim;
    before.platform.dfu().has_staged_hash = true;
    before.platform.dfu().staged_image_hash = kFull;
    REQUIRE(before.setup() == Status::Ok);
    uint32_t t = 0;
    on_ground(before, t);
    before.send("{\"cmd\":\"dfu\",\"version\":\"0.1.0+12\"}");
    before.run(t, t + 200);
    t += 200;
    config(before).confirm();
    land_upload(before, t);
    before.run(t, t + power::kParkMs + power::kReleaseSettleMs + 500);
    REQUIRE(before.platform.dfu().triggered == 1);

    Rebooted after(before, kRunning, /*confirmed=*/true);
    after.rig.platform.dfu().has_running_hash = true;
    after.rig.platform.dfu().running_image_hash = kSlim;
    REQUIRE(after.rig.setup() == Status::Ok);
    CHECK(config(after.rig).image_state() == dfu::ImageState::Reverted);

    Rebooted landed(before, kRunning, /*confirmed=*/true);
    landed.rig.platform.dfu().has_running_hash = true;
    landed.rig.platform.dfu().running_image_hash = kFull;
    REQUIRE(landed.rig.setup() == Status::Ok);
    CHECK(config(landed.rig).image_state() == dfu::ImageState::Confirmed);
    CHECK_FALSE(attempt_recorded(landed.rig));
}

TEST_CASE("product: the image that lands forgets the attempt once it has confirmed itself") {
    Rig before;
    stage_versions(before);
    REQUIRE(before.setup() == Status::Ok);
    install_and_swap(before);
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

// The finished upload is held in RAM, so a restart costs the pilot the upload and not a boot.
TEST_CASE("product: an image staged before a restart is not installed by the next window") {
    Rig before;
    stage_versions(before);
    REQUIRE(before.setup() == Status::Ok);

    Rebooted after(before, kRunning, /*confirmed=*/true);
    after.rig.platform.dfu().has_staged = true;
    after.rig.platform.dfu().staged = kStaged;
    REQUIRE(after.rig.setup() == Status::Ok);
    uint32_t t = 0;
    open_upload_window(after.rig, t);
    after.rig.run(t, t + 2000);
    CHECK(config(after.rig).receiving_firmware());
    CHECK(after.rig.platform.dfu().triggered == 0);
}

TEST_CASE("product: an image nobody staged over the air clears a stale attempt") {
    Rig before;
    stage_versions(before);
    REQUIRE(before.setup() == Status::Ok);
    install_and_swap(before);
    REQUIRE(attempt_recorded(before));

    // a .uf2 dropped on the bootloader volume is neither side of the attempt
    Rebooted flashed(before, ports::ImageVersion{0, 3, 0, 1}, /*confirmed=*/true);
    REQUIRE(flashed.rig.setup() == Status::Ok);
    CHECK(config(flashed.rig).image_state() == dfu::ImageState::Confirmed);
    CHECK_FALSE(attempt_recorded(flashed.rig));
}

// A reverted image that wrote over a newer blob lost the pilot's settings for good.
TEST_CASE("product: a set on an image older than its settings never writes over them") {
    Rig rig;
    const NewerBlob newer;
    REQUIRE(rig.platform.kv().write("settings", newer.bytes, sizeof(newer.bytes)) == Status::Ok);
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;

    set_callsign_over_the_link(rig, t, "F-JABC");
    CHECK(std::string(rig.settings().callsign) == "F-JABC");
    CHECK(holds(rig, "settings", newer.bytes, sizeof(newer.bytes)));

    Rebooted after(rig, kRunning, /*confirmed=*/true);
    REQUIRE(after.rig.setup() == Status::Ok);
    CHECK(std::string(after.rig.settings().callsign) == "F-JABC");
    CHECK(after.rig.product.config().settings_fallback() == settings::Fallback::Prior);
    CHECK(holds(after.rig, "settings", newer.bytes, sizeof(newer.bytes)));
}

TEST_CASE("product: an image with nothing of its own beside a newer blob starts from defaults") {
    Rig rig;
    const NewerBlob newer;
    REQUIRE(rig.platform.kv().write("settings", newer.bytes, sizeof(newer.bytes)) == Status::Ok);
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.product.config().settings_fallback() == settings::Fallback::Defaults);
    CHECK(rig.settings().alarm_volume == go::defaults().alarm_volume);
    CHECK(holds(rig, "settings", newer.bytes, sizeof(newer.bytes)));
}

TEST_CASE("product: settings that fell back to defaults are said on the self test and to a phone") {
    Rig rig;
    const NewerBlob newer;
    REQUIRE(rig.platform.kv().write("settings", newer.bytes, sizeof(newer.bytes)) == Status::Ok);
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(std::string(storage_row(rig).detail) == go::kStorageOnDefaults);

    rig.raise_link();
    rig.run(0, 200);
    const std::string frame = update_frame(rig);
    CHECK(frame.find("\"image\":\"confirmed\"") != std::string::npos);
    CHECK(frame.find("\"settings\":\"defaults\"") != std::string::npos);
}

TEST_CASE("product: a sector that lost a bit is written over, not kept as a newer image's") {
    Rig rig;
    NewerBlob torn;
    torn.bytes[3] ^= 0x01;
    REQUIRE(rig.platform.kv().write("settings", torn.bytes, sizeof(torn.bytes)) == Status::Ok);
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.product.config().settings_fallback() == settings::Fallback::Defaults);
    uint32_t t = 0;

    set_callsign_over_the_link(rig, t, "F-JABC");
    uint8_t blob[64];
    size_t n = 0;
    REQUIRE(rig.platform.kv().read("settings", blob, sizeof(blob), n) == Status::Ok);
    go::Settings stored;
    REQUIRE(go::from_blob(blob, n, stored) == Status::Ok);
    CHECK(std::string(stored.callsign) == "F-JABC");
}

TEST_CASE("product: a revert puts back the settings the pilot had when the swap began") {
    Rig before;
    stage_versions(before);
    REQUIRE(before.setup() == Status::Ok);
    uint32_t t = 0;
    set_callsign_over_the_link(before, t, "D-KXYZ");
    install_and_swap(before);
    REQUIRE(before.platform.dfu().triggered == 1);

    Rebooted landed(before, kStaged, /*confirmed=*/false);
    const NewerBlob newer;
    REQUIRE(landed.rig.platform.kv().write("settings", newer.bytes, sizeof(newer.bytes)) ==
            Status::Ok);

    Rebooted reverted(landed.rig, kRunning, /*confirmed=*/true);
    REQUIRE(reverted.rig.setup() == Status::Ok);
    CHECK(config(reverted.rig).image_state() == dfu::ImageState::Reverted);
    CHECK(std::string(reverted.rig.settings().callsign) == "D-KXYZ");
    CHECK(reverted.rig.product.config().settings_fallback() == settings::Fallback::Prior);

    reverted.rig.raise_link();
    reverted.rig.run(0, 200);
    const std::string frame = update_frame(reverted.rig);
    CHECK(frame.find("\"image\":\"reverted\"") != std::string::npos);
    CHECK(frame.find("\"settings\":\"prior\"") != std::string::npos);
}
