// A press held from before a link prompt stood is never spent on its confirmation (#245).
#include <optional>
#include <string>

#include "core/comms/config.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/identity.h"
#include "test/support/activation_vectors.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

// INFO: fc 11nov26 what Chrome negotiates, wide enough for the challenge reply
constexpr uint16_t kChromePayloadBytes = 495;

// INFO: fc 11nov26 with the passes either side the hold stays under power::kLongPressHoldMs
constexpr uint32_t kHeldThroughPromptMs = 1500;

constexpr comms::Pending kPrompts[] = {
    comms::Pending::Set,      comms::Pending::Dfu,           comms::Pending::Recovery,
    comms::Pending::PowerOff, comms::Pending::GnssColdStart, comms::Pending::Register,
    comms::Pending::EraseLog,
};

std::optional<uint16_t> serial_for(comms::Pending pending) {
    if (pending == comms::Pending::Register) return std::nullopt;
    return platform::host::Identity::kSerial;
}

struct Unit {
    explicit Unit(comms::Pending asked)
        : pending(asked), rig(platform::host::Platform::kFullyFitted, serial_for(asked)) {
        rig.platform.dfu().running_version_valid = true;
        rig.platform.dfu().running_image_version = ports::ImageVersion{0, 3, 0, 1234};
        rig.platform.link().declare_payload_bytes(kChromePayloadBytes);
        REQUIRE(rig.setup() == Status::Ok);
        pass(500);
        if (pending == comms::Pending::Register) {
            rig.platform.identity().queue_random(vectors.bytes<16>("unit_nonce"));
            rig.send("{\"command\":\"challenge\"}");
            pass(300);
        }
    }

    void pass(uint32_t for_ms) {
        rig.run(now_ms, now_ms + for_ms);
        now_ms += for_ms + 50;
    }

    void ask() {
        switch (pending) {
            case comms::Pending::Set: rig.send("{\"command\":\"set\",\"alarm_volume\":2}"); return;
            case comms::Pending::Dfu:
                rig.send("{\"command\":\"dfu\",\"version\":\"0.3.1+15\"}");
                return;
            case comms::Pending::Recovery: rig.send("{\"command\":\"recovery\"}"); return;
            case comms::Pending::PowerOff: rig.send("{\"command\":\"power_off\"}"); return;
            case comms::Pending::GnssColdStart:
                rig.send("{\"command\":\"gnss_cold_start\"}");
                return;
            case comms::Pending::Register: {
                const std::string command = "{\"command\":\"register\",\"server_nonce\":\"" +
                                            vectors.text("server_nonce") + "\",\"signature\":\"" +
                                            vectors.text("register_signature") + "\"}";
                rig.send(command.c_str());
                return;
            }
            case comms::Pending::EraseLog: rig.send_log("{\"command\":\"erase\"}"); return;
            case comms::Pending::Capture:
            case comms::Pending::None: return;
        }
    }

    void hold_through_the_prompt() {
        rig.platform.board_gpio().button_down = true;
        pass(100);
        ask();
        pass(kHeldThroughPromptMs);
        REQUIRE(rig.product.screen().prompt() == pending);
        rig.platform.board_gpio().button_down = false;
    }

    bool prompt_stands() { return rig.product.config_link().config().pending() == pending; }

    bool allowed_anything() {
        const std::string said = rig.last_on(events::Endpoint::Config);
        return said.find("\"acknowledge\":true") != std::string::npos ||
               said.find("registration") != std::string::npos ||
               rig.product.config_link().config().receiving_firmware() ||
               rig.product.shutdown().going_down() || rig.product.flight_log().erasing();
    }

    comms::Pending pending;
    ActivationVectors vectors;
    Rig rig;
    uint32_t now_ms{0};
};

}  // namespace

TEST_CASE("product: a press held through a prompt's arrival, and one press after, allow nothing") {
    for (const comms::Pending pending : kPrompts) {
        CAPTURE(pending);
        Unit unit(pending);
        const go::AlarmVolume volume = unit.rig.settings().alarm_volume;
        unit.hold_through_the_prompt();
        unit.pass(100);
        unit.rig.press(unit.now_ms);
        unit.pass(2000);
        CHECK(unit.prompt_stands());
        CHECK_FALSE(unit.allowed_anything());
        CHECK(unit.rig.settings().alarm_volume == volume);
    }
}

TEST_CASE("product: after a held press the gesture arms only once the thumb has been up a while") {
    for (const comms::Pending pending : kPrompts) {
        CAPTURE(pending);
        Unit unit(pending);
        unit.hold_through_the_prompt();
        unit.pass(250);
        unit.rig.double_press(unit.now_ms);
        unit.pass(100);
        CHECK(unit.prompt_stands());
        CHECK_FALSE(unit.allowed_anything());

        unit.pass(800);
        unit.rig.double_press(unit.now_ms);
        unit.pass(100);
        CHECK_FALSE(unit.prompt_stands());
        CHECK(unit.allowed_anything());
    }
}

// GPT's review: the release is seen at 2090 ms, so the gesture arms at 2690, the pass that
// publishes a down which began at 2650.
TEST_CASE("product: a press that began before the gesture armed is never spent on it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.settings().alarm_volume = go::AlarmVolume::Soft;
    rig.run(0, 500);
    rig.platform.board_gpio().button_down = true;
    rig.run(550, 650);
    rig.send("{\"command\":\"set\",\"alarm_volume\":2}");
    rig.run(700, 2000);
    REQUIRE(rig.product.screen().prompt() == comms::Pending::Set);
    rig.platform.board_gpio().button_down = false;
    rig.run(2050, 2050);
    rig.run(2090, 2610);
    rig.platform.board_gpio().button_down = true;
    rig.run(2650, 2690, 40);
    rig.platform.board_gpio().button_down = false;
    rig.run(2730, 2770, 40);
    uint32_t now_ms = 2810;
    rig.press(now_ms);
    rig.run(now_ms, now_ms + 100);
    CHECK(rig.product.config_link().config().pending() == comms::Pending::Set);
    CHECK(rig.settings().alarm_volume == go::AlarmVolume::Soft);
}

// Opus's review: the shutdown ignores the first hold after boot, so its release is the last one.
TEST_CASE("product: after a hold carried from boot the gesture waits a window from the release") {
    Rig rig;
    rig.platform.board_gpio().button_down = true;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    rig.send("{\"command\":\"dfu\",\"version\":\"0.2.0+15\"}");
    rig.run(now_ms, now_ms + 2600);
    now_ms += 2650;
    REQUIRE(rig.product.screen().prompt() == comms::Pending::Dfu);
    REQUIRE_FALSE(rig.product.shutdown().going_down());
    rig.platform.board_gpio().button_down = false;
    rig.run(now_ms, now_ms + 100);
    now_ms += 150;
    rig.double_press(now_ms);
    rig.run(now_ms, now_ms + 100);
    now_ms += 150;
    CHECK(rig.product.config_link().config().pending() == comms::Pending::Dfu);
    CHECK_FALSE(rig.product.config_link().config().receiving_firmware());

    rig.run(now_ms, now_ms + 800);
    now_ms += 850;
    rig.double_press(now_ms);
    rig.run(now_ms, now_ms + 100);
    CHECK(rig.product.config_link().config().receiving_firmware());
}

// GPT's review: a phone that drops and stages the same kind of prompt within one pass replaces it.
TEST_CASE("product: a replaced prompt cannot spend a hold begun on the one it replaced") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.settings().alarm_volume = go::AlarmVolume::Soft;
    rig.run(0, 500);
    rig.send("{\"command\":\"set\",\"alarm_volume\":0}");
    rig.run(550, 3500);
    REQUIRE(rig.product.screen().prompt() == comms::Pending::Set);
    rig.platform.board_gpio().button_down = true;
    rig.run(3550, 3650);
    rig.drop_link();
    rig.raise_link(2);
    rig.send_from(2, "{\"command\":\"set\",\"alarm_volume\":2}");
    rig.run(3700, 4500);
    REQUIRE(rig.product.config_link().config().claim_holder() == 2);
    REQUIRE(rig.product.screen().prompt() == comms::Pending::Set);
    rig.platform.board_gpio().button_down = false;
    rig.run(4550, 4650);
    uint32_t now_ms = 4700;
    rig.press(now_ms);
    rig.run(now_ms, now_ms + 100);
    CHECK(rig.product.config_link().config().pending() == comms::Pending::Set);
    CHECK(rig.settings().alarm_volume == go::AlarmVolume::Soft);
}

// GPT's re-review: the longest gap before the double press is 560 ms, from 2090 to 2650.
TEST_CASE("product: after a press that began before arming, a double press waits a quiet window") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.settings().alarm_volume = go::AlarmVolume::Soft;
    rig.run(0, 500);
    rig.platform.board_gpio().button_down = true;
    rig.run(550, 650);
    rig.send("{\"command\":\"set\",\"alarm_volume\":2}");
    rig.run(700, 2000);
    REQUIRE(rig.product.screen().prompt() == comms::Pending::Set);
    rig.platform.board_gpio().button_down = false;
    rig.run(2050, 2050);
    rig.run(2090, 2610);
    rig.platform.board_gpio().button_down = true;
    rig.run(2650, 2690, 40);
    rig.platform.board_gpio().button_down = false;
    rig.run(2730, 2770, 40);
    uint32_t now_ms = 2810;
    rig.double_press(now_ms);
    rig.run(now_ms, now_ms + 100);
    CHECK(rig.product.config_link().config().pending() == comms::Pending::Set);
    CHECK(rig.settings().alarm_volume == go::AlarmVolume::Soft);

    rig.run(now_ms, now_ms + 750);
    now_ms += 800;
    rig.double_press(now_ms);
    rig.run(now_ms, now_ms + 100);
    CHECK(rig.settings().alarm_volume == go::AlarmVolume::Loud);
}
