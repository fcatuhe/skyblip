// The whole product taking an update; the bootloader is the one thing the host cannot run.
#include <cstring>
#include <optional>
#include <string>

#include "core/settings/blob.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/installing.h"
#include "products/skyblip_go/pages/recovery.h"
#include "products/skyblip_go/pages/self_test.h"
#include "runtime/tasks.h"
#include "test/support/glass_text.h"
#include "test/support/product_rig.h"
#include "test/support/update_rig.h"

using namespace skyblip;

namespace {

constexpr ports::ImageVersion kRunning{0, 1, 0, 12};
constexpr ports::ImageVersion kStaged{0, 2, 0, 15};

void pass(Rig& rig, uint32_t& now_ms) {
    now_ms += 50;
    rig.run(now_ms, now_ms);
}

void open_upload_window(Rig& rig, uint32_t& now_ms) {
    on_ground(rig, now_ms);
    rig.send("{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}");
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    config(rig).confirm();
}

void climb_until_the_link_sees_it(Rig& rig, uint32_t& now_ms) {
    for (int second = 0; second < 20; second++) {
        gnss::GnssSolution climbing{};
        climbing.fix_valid = true;
        climbing.speed_mm_s = 50000;
        climbing.altitude_msl_mm = 1200 * 1000;
        climbing.sentences = 1;
        rig.product.bus().gnss.push(climbing);
        for (int i = 0; i < 10; i++) {
            pass(rig, now_ms);
            if (config(rig).flight_state() == flight::FlightState::Airborne) return;
        }
    }
}

void stage_versions(Rig& rig) {
    rig.platform.dfu().running_version_valid = true;
    rig.platform.dfu().running_image_version = kRunning;
    rig.platform.dfu().staged_version_valid = true;
    rig.platform.dfu().staged_image_version = kStaged;
}

void land_upload(Rig& rig, uint32_t& now_ms) {
    rig.platform.dfu().finished_upload = true;
    pass(rig, now_ms);
}

void install_and_swap(Rig& rig) {
    uint32_t now_ms = 0;
    open_upload_window(rig, now_ms);
    land_upload(rig, now_ms);
    rig.run(now_ms, now_ms + power::kParkSettleMs + power::kReleaseSettleMs + 500);
}

// The unsolicited frame a phone gets on connecting, wherever the status push landed.
std::string update_frame(Rig& rig) {
    for (const auto& frame : rig.platform.link().sent)
        if (frame.bytes.find("\"cmd\":\"update\"") != std::string::npos) return frame.bytes;
    return "";
}

bool attempt_recorded(Rig& rig) {
    uint8_t blob[dfu::kUpdateRecordBytes];
    size_t count = 0;
    return rig.platform.key_value().read("update", blob, sizeof(blob), count) == Status::Ok;
}

// The device after the bootloader has run: same flash, a fresh boot, and what slot 1 kept.
struct Rebooted {
    Rig rig;
    Rebooted(Rig& before, ports::ImageVersion running, bool confirmed,
             std::optional<ports::ImageVersion> slot1 = std::nullopt) {
        rig.platform.key_value() = before.platform.key_value();
        rig.platform.dfu().running_version_valid = true;
        rig.platform.dfu().running_image_version = running;
        rig.platform.dfu().image_confirmed = confirmed;
        rig.platform.dfu().staged_version_valid = slot1.has_value();
        if (slot1) rig.platform.dfu().staged_image_version = *slot1;
    }
};

const go::SelfTestPart& storage_row(Rig& rig) {
    for (int i = 0; i < go::kSelfTestPartCount; i++)
        if (std::string(rig.product.self_test_rows()[i].name) == "STORAGE")
            return rig.product.self_test_rows()[i];
    return rig.product.self_test_rows()[0];
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

bool holds(Rig& rig, const char* key, const uint8_t* blob, size_t blob_bytes) {
    uint8_t stored[64];
    size_t count = 0;
    if (rig.platform.key_value().read(key, stored, sizeof(stored), count) != Status::Ok)
        return false;
    return count == blob_bytes && std::memcmp(stored, blob, blob_bytes) == 0;
}

void set_callsign_over_the_link(Rig& rig, uint32_t& now_ms, const char* callsign) {
    on_ground(rig, now_ms);
    std::string json = "{\"cmd\":\"set\",\"callsign\":\"";
    json += callsign;
    json += "\"}";
    rig.send(json.c_str());
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    config(rig).confirm();
    rig.run(now_ms, now_ms + 3000);
    now_ms += 3000;
}

}  // namespace

TEST_CASE("product: an upload that lands parks the device and paints the glass before the swap") {
    Rig rig;
    stage_versions(rig);
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    open_upload_window(rig, now_ms);
    rig.run(now_ms, now_ms + 3000);
    now_ms += 3000;
    CHECK(reads_at(rig.platform.chips().epd.framebuffer(), go::kInstallingLeftX,
                   go::kInstallingTitleY, go::kReceivingTitle, 2));
    CHECK(rig.platform.dfu().triggered == 0);

    land_upload(rig, now_ms);
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::Install);
    CHECK(rig.product.shutdown().phase() == power::ShutdownPhase::Parking);
    CHECK(rig.product.installing());
    CHECK(rig.product.board().rf().sleeps() == 1);
    CHECK_FALSE(rig.product.screen().powered());
    CHECK_FALSE(config(rig).install_requested());

    // the panel is still clocking its refresh, and nothing is on record yet
    CHECK(rig.platform.dfu().triggered == 0);
    CHECK_FALSE(attempt_recorded(rig));

    rig.run(now_ms, now_ms + power::kParkSettleMs + power::kReleaseSettleMs + 5000);
    CHECK(reads_at(rig.platform.chips().epd.framebuffer(), go::kInstallingLeftX,
                   go::kInstallingTitleY, go::kInstallingTitle, 2));
    CHECK(rig.platform.dfu().triggered == 1);
    CHECK_FALSE(rig.product.ready_to_power_off());
    CHECK(attempt_recorded(rig));

    uint8_t blob[dfu::kUpdateRecordBytes];
    size_t count = 0;
    REQUIRE(rig.platform.key_value().read("update", blob, sizeof(blob), count) == Status::Ok);
    dfu::UpdateRecord record;
    REQUIRE(dfu::from_blob(blob, count, record));
    CHECK(record.from == kRunning);
    CHECK(record.to == kStaged);
}

TEST_CASE("product: with no watchdog running a recovery paints the bootloader page, then reboots") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    on_ground(rig, now_ms);

    rig.send("{\"cmd\":\"recovery\"}");
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    REQUIRE(config(rig).pending() == comms::Pending::Recovery);

    config(rig).confirm();
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::Recovery);
    CHECK(rig.product.board().rf().sleeps() == 1);
    CHECK(rig.platform.dfu().recoveries == 0);

    rig.run(now_ms, now_ms + power::kParkSettleMs + power::kReleaseSettleMs + 5000);
    const ui::Canvas& glass = rig.platform.chips().epd.framebuffer();
    CHECK(reads_at(glass, go::kInstallingLeftX, go::kInstallingTitleY, go::kRecoveryTitle, 2));
    CHECK(reads_at(glass, go::kInstallingLeftX, go::installing_body_y(0), go::kRecoveryRunning, 1));
    CHECK(rig.platform.dfu().recoveries == 1);
    CHECK_FALSE(rig.product.ready_to_power_off());
}

TEST_CASE("product: under a running watchdog a recovery asks for the press, then drops the rails") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    REQUIRE(rig.platform.watchdog().arm(runtime::kHardwareWatchdogCeilingMs) == Status::Ok);
    uint32_t now_ms = 0;
    on_ground(rig, now_ms);

    rig.send("{\"cmd\":\"recovery\"}");
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    config(rig).confirm();
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::Recovery);
    CHECK_FALSE(rig.product.ready_to_power_off());
    CHECK(rig.platform.dfu().recoveries == 0);

    rig.run(now_ms, now_ms + power::kParkSettleMs + power::kReleaseSettleMs + 5000);
    const ui::Canvas& glass = rig.platform.chips().epd.framebuffer();
    CHECK(reads_at(glass, go::kInstallingLeftX, go::kInstallingTitleY, go::kRecoveryTitle, 2));
    CHECK(reads_at(glass, go::kInstallingLeftX, go::installing_body_y(0), go::kRecoveryAwaitsPress,
                   1));
    CHECK(rig.platform.dfu().recoveries == 1);
    CHECK(rig.product.ready_to_power_off());
    CHECK(std::string(power::to_string(rig.product.shutdown().reason())) == "RECOVERY");
}

TEST_CASE("product: the SMP hook's gate opens on the pass after the window is confirmed") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    open_upload_window(rig, now_ms);
    REQUIRE(config(rig).upload_allowed());
    CHECK_FALSE(rig.platform.dfu().upload_allowed_published);

    pass(rig, now_ms);
    CHECK(rig.platform.dfu().upload_allowed_published);
}

// A chunk the MCUmgr work queue reads against a stale gate is a chunk written in flight.
TEST_CASE("product: the pass that learns of the take-off closes the SMP hook's gate in that pass") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    open_upload_window(rig, now_ms);
    pass(rig, now_ms);
    REQUIRE(rig.platform.dfu().upload_allowed_published);

    climb_until_the_link_sees_it(rig, now_ms);
    REQUIRE(config(rig).flight_state() == flight::FlightState::Airborne);
    CHECK_FALSE(rig.platform.dfu().upload_allowed_published);
}

TEST_CASE("product: a single press on the receiving page closes the window, and nothing installs") {
    Rig rig;
    stage_versions(rig);
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    open_upload_window(rig, now_ms);
    rig.run(now_ms, now_ms + 3000);
    now_ms += 3000;
    REQUIRE(config(rig).receiving_firmware());

    rig.press(now_ms);
    rig.run(now_ms, now_ms + 3000);
    now_ms += 3000;
    CHECK_FALSE(config(rig).receiving_firmware());
    CHECK(rig.last_on(events::Endpoint::Config).find("cancelled") != std::string::npos);
    CHECK_FALSE(reads_at(rig.platform.chips().epd.framebuffer(), go::kInstallingLeftX,
                         go::kInstallingTitleY, go::kReceivingTitle, 2));

    land_upload(rig, now_ms);
    rig.run(now_ms, now_ms + 200);
    CHECK_FALSE(rig.product.shutdown().going_down());
    CHECK(rig.platform.dfu().triggered == 0);
}

TEST_CASE("product: an upload that lands on a critical cell is refused and nothing parks") {
    Rig rig;
    stage_versions(rig);
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    open_upload_window(rig, now_ms);
    rig.platform.battery().divider_mv = 3400;
    rig.run(now_ms, now_ms + 8000);
    now_ms += 8000;
    REQUIRE(rig.state().power.level == power::PowerLevel::Critical);

    land_upload(rig, now_ms);
    rig.run(now_ms, now_ms + 200);
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

    Rebooted after(before, kRunning, /*confirmed=*/true, kStaged);
    REQUIRE(after.rig.setup() == Status::Ok);
    CHECK(config(after.rig).image_state() == dfu::ImageState::Reverted);

    after.rig.raise_link();
    after.rig.run(0, 200);
    const std::string frame = update_frame(after.rig);
    CHECK(frame.find("\"image\":\"reverted\"") != std::string::npos);
    CHECK(frame.find("\"from\":\"0.1.0+12\"") != std::string::npos);
    CHECK(frame.find("\"to\":\"0.2.0+15\"") != std::string::npos);

    Rebooted later(after.rig, kRunning, /*confirmed=*/true, kStaged);
    REQUIRE(later.rig.setup() == Status::Ok);
    CHECK(config(later.rig).image_state() == dfu::ImageState::Reverted);
}

// The full image of the running version, sent to give a fitted hub its image: by version it landed.
TEST_CASE("product: a same-version image the bootloader reverted reads as reverted by its hash") {
    constexpr ports::ImageHash kSlim{{0x51, 0x1A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}};
    constexpr ports::ImageHash kFull{{0xF0, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02}};
    Rig before;
    stage_versions(before);
    before.platform.dfu().staged_image_version = kRunning;
    before.platform.dfu().running_hash_valid = true;
    before.platform.dfu().running_image_hash = kSlim;
    before.platform.dfu().staged_hash_valid = true;
    before.platform.dfu().staged_image_hash = kFull;
    REQUIRE(before.setup() == Status::Ok);
    uint32_t now_ms = 0;
    on_ground(before, now_ms);
    before.send("{\"cmd\":\"dfu\",\"version\":\"0.1.0+12\"}");
    before.run(now_ms, now_ms + 200);
    now_ms += 200;
    config(before).confirm();
    land_upload(before, now_ms);
    before.run(now_ms, now_ms + power::kParkSettleMs + power::kReleaseSettleMs + 500);
    REQUIRE(before.platform.dfu().triggered == 1);

    Rebooted after(before, kRunning, /*confirmed=*/true, kRunning);
    after.rig.platform.dfu().running_hash_valid = true;
    after.rig.platform.dfu().running_image_hash = kSlim;
    after.rig.platform.dfu().staged_hash_valid = true;
    after.rig.platform.dfu().staged_image_hash = kFull;
    REQUIRE(after.rig.setup() == Status::Ok);
    CHECK(config(after.rig).image_state() == dfu::ImageState::Reverted);

    Rebooted landed(before, kRunning, /*confirmed=*/true);
    landed.rig.platform.dfu().running_hash_valid = true;
    landed.rig.platform.dfu().running_image_hash = kFull;
    REQUIRE(landed.rig.setup() == Status::Ok);
    CHECK(config(landed.rig).image_state() == dfu::ImageState::Confirmed);
    CHECK_FALSE(attempt_recorded(landed.rig));
}

TEST_CASE(
    "product: an update the bootloader threw away is told apart from one that ran and failed") {
    Rig before;
    stage_versions(before);
    REQUIRE(before.setup() == Status::Ok);
    install_and_swap(before);
    REQUIRE(before.platform.dfu().triggered == 1);

    Rebooted after(before, kRunning, /*confirmed=*/true);
    REQUIRE(after.rig.setup() == Status::Ok);
    CHECK(config(after.rig).image_state() == dfu::ImageState::Refused);

    after.rig.raise_link();
    after.rig.run(0, 200);
    const std::string frame = update_frame(after.rig);
    CHECK(frame.find("\"image\":\"refused\"") != std::string::npos);
    CHECK(frame.find("\"to\":\"0.2.0+15\"") != std::string::npos);
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
    after.rig.platform.dfu().staged_version_valid = true;
    after.rig.platform.dfu().staged_image_version = kStaged;
    REQUIRE(after.rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    open_upload_window(after.rig, now_ms);
    after.rig.run(now_ms, now_ms + 2000);
    CHECK(config(after.rig).receiving_firmware());
    CHECK(after.rig.platform.dfu().triggered == 0);
}

namespace {
std::string ask_for(Rig& rig, const char* version) {
    uint32_t now_ms = 0;
    on_ground(rig, now_ms);
    std::string json = "{\"cmd\":\"dfu\",\"version\":\"";
    json += version;
    json += "\"}";
    rig.send(json.c_str());
    rig.run(now_ms, now_ms + 200);
    return rig.last_on(events::Endpoint::Config);
}

std::string land(Rig& rig) {
    uint32_t now_ms = 0;
    open_upload_window(rig, now_ms);
    land_upload(rig, now_ms);
    return rig.last_on(events::Endpoint::Config);
}

void sign(Rig& rig, uint8_t trusted, uint8_t signer) {
    rig.platform.dfu().running_key_valid = true;
    rig.platform.dfu().running_signer.fill(trusted);
    rig.platform.dfu().staged_key_valid = true;
    rig.platform.dfu().staged_signer.fill(signer);
}
}  // namespace

TEST_CASE(
    "product: an image signed by a key the bootloader does not trust lands and is not installed") {
    Rig rig;
    stage_versions(rig);
    sign(rig, 0x21, 0x42);
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(land(rig).find("\"reason\":\"wrong_key\"") != std::string::npos);
    CHECK_FALSE(config(rig).receiving_firmware());
    CHECK(rig.platform.dfu().triggered == 0);
}

TEST_CASE("product: an image with no key hash at all is refused as the wrong key") {
    Rig rig;
    stage_versions(rig);
    sign(rig, 0x21, 0x21);
    rig.platform.dfu().staged_key_valid = false;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(land(rig).find("\"reason\":\"wrong_key\"") != std::string::npos);
    CHECK(rig.platform.dfu().triggered == 0);
}

TEST_CASE("product: a release unit refuses to ask for an older image, and asks for the same one") {
    Rig older;
    stage_versions(older);
    REQUIRE(older.setup() == Status::Ok);
    CHECK(ask_for(older, "0.1.0+11").find("\"reason\":\"older\"") != std::string::npos);
    CHECK(config(older).pending() == comms::Pending::None);

    Rig same;
    stage_versions(same);
    REQUIRE(same.setup() == Status::Ok);
    CHECK(ask_for(same, "0.1.0+12").find("confirm_dfu") != std::string::npos);
    CHECK(config(same).pending() == comms::Pending::Dfu);
}

TEST_CASE("product: a development unit asks for an older image") {
    Rig rig;
    stage_versions(rig);
    rig.platform.dfu().allows_downgrade = true;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(ask_for(rig, "0.1.0+3").find("confirm_dfu") != std::string::npos);
    CHECK(config(rig).pending() == comms::Pending::Dfu);
}

TEST_CASE("product: the update frame names the trusted key and, on a development unit, the rule") {
    Rig release;
    sign(release, 0x21, 0x21);
    release.platform.dfu().running_signer[1] = 0x26;
    REQUIRE(release.setup() == Status::Ok);
    release.send("{\"cmd\":\"update\"}");
    release.run(0, 200);
    const std::string said = release.last_on(events::Endpoint::Config);
    CHECK(said.find("\"key\":\"21262121\"") != std::string::npos);
    CHECK(said.find("\"downgrade\"") == std::string::npos);

    Rig development;
    sign(development, 0x21, 0x21);
    development.platform.dfu().allows_downgrade = true;
    REQUIRE(development.setup() == Status::Ok);
    development.send("{\"cmd\":\"update\"}");
    development.run(0, 200);
    CHECK(development.last_on(events::Endpoint::Config).find("\"downgrade\":true") !=
          std::string::npos);
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
    REQUIRE(rig.platform.key_value().write("settings", newer.bytes, sizeof(newer.bytes)) ==
            Status::Ok);
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;

    set_callsign_over_the_link(rig, now_ms, "F-JABC");
    CHECK(std::string(rig.settings().callsign) == "F-JABC");
    CHECK(holds(rig, "settings", newer.bytes, sizeof(newer.bytes)));

    Rebooted after(rig, kRunning, /*confirmed=*/true);
    REQUIRE(after.rig.setup() == Status::Ok);
    CHECK(std::string(after.rig.settings().callsign) == "F-JABC");
    CHECK(after.rig.product.config_link().settings_fallback() == settings::Fallback::Prior);
    CHECK(holds(after.rig, "settings", newer.bytes, sizeof(newer.bytes)));
}

TEST_CASE("product: an image with nothing of its own beside a newer blob starts from defaults") {
    Rig rig;
    const NewerBlob newer;
    REQUIRE(rig.platform.key_value().write("settings", newer.bytes, sizeof(newer.bytes)) ==
            Status::Ok);
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.product.config_link().settings_fallback() == settings::Fallback::Defaults);
    CHECK(rig.settings().alarm_volume == go::defaults().alarm_volume);
    CHECK(holds(rig, "settings", newer.bytes, sizeof(newer.bytes)));
}

TEST_CASE("product: settings that fell back to defaults are said on the self test and to a phone") {
    Rig rig;
    const NewerBlob newer;
    REQUIRE(rig.platform.key_value().write("settings", newer.bytes, sizeof(newer.bytes)) ==
            Status::Ok);
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
    REQUIRE(rig.platform.key_value().write("settings", torn.bytes, sizeof(torn.bytes)) ==
            Status::Ok);
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.product.config_link().settings_fallback() == settings::Fallback::Defaults);
    uint32_t now_ms = 0;

    set_callsign_over_the_link(rig, now_ms, "F-JABC");
    uint8_t blob[64];
    size_t count = 0;
    REQUIRE(rig.platform.key_value().read("settings", blob, sizeof(blob), count) == Status::Ok);
    go::Settings stored;
    REQUIRE(go::from_blob(blob, count, stored) == Status::Ok);
    CHECK(std::string(stored.callsign) == "F-JABC");
}

TEST_CASE("product: a revert puts back the settings the pilot had when the swap began") {
    Rig before;
    stage_versions(before);
    REQUIRE(before.setup() == Status::Ok);
    uint32_t now_ms = 0;
    set_callsign_over_the_link(before, now_ms, "D-KXYZ");
    install_and_swap(before);
    REQUIRE(before.platform.dfu().triggered == 1);

    Rebooted landed(before, kStaged, /*confirmed=*/false);
    const NewerBlob newer;
    REQUIRE(landed.rig.platform.key_value().write("settings", newer.bytes, sizeof(newer.bytes)) ==
            Status::Ok);

    Rebooted reverted(landed.rig, kRunning, /*confirmed=*/true, kStaged);
    REQUIRE(reverted.rig.setup() == Status::Ok);
    CHECK(config(reverted.rig).image_state() == dfu::ImageState::Reverted);
    CHECK(std::string(reverted.rig.settings().callsign) == "D-KXYZ");
    CHECK(reverted.rig.product.config_link().settings_fallback() == settings::Fallback::Prior);

    reverted.rig.raise_link();
    reverted.rig.run(0, 200);
    const std::string frame = update_frame(reverted.rig);
    CHECK(frame.find("\"image\":\"reverted\"") != std::string::npos);
    CHECK(frame.find("\"settings\":\"prior\"") != std::string::npos);
}
