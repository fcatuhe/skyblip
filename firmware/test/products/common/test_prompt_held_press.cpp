// A press held from before a link prompt stood is never spent on its confirmation (#245).
#include <optional>
#include <string>

#include "doctest/doctest.h"
#include "hardware/platform/host/identity.h"
#include "products/common/services/config_service.h"
#include "test/support/activation_vectors.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

// INFO: fc 11nov26 what Chrome negotiates, wide enough for the challenge reply
constexpr uint16_t kChromePayloadBytes = 495;

// INFO: fc 11nov26 with the passes either side the hold stays under power::kLongPressHoldMs
constexpr uint32_t kHeldThroughPromptMs = 1500;

constexpr common::Pending kPrompts[] = {
    common::Pending::Set,      common::Pending::Dfu,           common::Pending::Recovery,
    common::Pending::PowerOff, common::Pending::GnssColdStart, common::Pending::Register,
    common::Pending::EraseLog, common::Pending::Pair,
};

std::optional<uint16_t> serial_for(common::Pending pending) {
    if (pending == common::Pending::Register) return std::nullopt;
    return platform::host::Identity::kSerial;
}

struct Unit {
    explicit Unit(common::Pending asked)
        : pending(asked), rig(platform::host::Platform::kFullyFitted, serial_for(asked)) {
        rig.platform.dfu().running_version_valid = true;
        rig.platform.dfu().running_image_version = ports::ImageVersion{0, 3, 0, 1234};
        rig.platform.link().declare_payload_bytes(kChromePayloadBytes);
        REQUIRE(rig.setup() == Status::Ok);
        pass(500);
        if (pending == common::Pending::Register) {
            rig.platform.random().queue(vectors.bytes<16>("unit_nonce"));
            rig.send("{\"command\":\"challenge\"}");
            pass(300);
        }
    }

    void pass(uint32_t for_ms) {
        rig.run_for(for_ms);
        rig.now_ms += 50;
    }

    void ask() {
        switch (pending) {
            case common::Pending::Set: rig.send("{\"command\":\"set\",\"alarm\":3}"); return;
            case common::Pending::Dfu:
                rig.send("{\"command\":\"dfu\",\"version\":\"0.3.1+15\"}");
                return;
            case common::Pending::Recovery: rig.send("{\"command\":\"recovery\"}"); return;
            case common::Pending::PowerOff: rig.send("{\"command\":\"power_off\"}"); return;
            case common::Pending::GnssColdStart:
                rig.send("{\"command\":\"gnss_cold_start\"}");
                return;
            case common::Pending::Register: {
                const std::string command = "{\"command\":\"register\",\"server_nonce\":\"" +
                                            vectors.text("server_nonce") + "\",\"signature\":\"" +
                                            vectors.text("register_signature") + "\"}";
                rig.send(command.c_str());
                return;
            }
            case common::Pending::EraseLog: rig.send_log("{\"command\":\"erase\"}"); return;
            case common::Pending::Pair: rig.send("{\"command\":\"pair\",\"tag\":16170}"); return;
            case common::Pending::Capture:
            case common::Pending::None: return;
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

    common::Pending pending;
    ActivationVectors vectors;
    Rig rig;
};

}  // namespace

TEST_CASE("product: a press held through a prompt's arrival, and one press after, allow nothing") {
    for (const common::Pending pending : kPrompts) {
        CAPTURE(pending);
        Unit unit(pending);
        const go::Alarm volume = unit.rig.settings().alarm;
        unit.hold_through_the_prompt();
        unit.pass(100);
        unit.rig.press();
        unit.pass(2000);
        CHECK(unit.prompt_stands());
        CHECK_FALSE(unit.allowed_anything());
        CHECK(unit.rig.settings().alarm == volume);
    }
}

TEST_CASE("product: after a held press the gesture arms only once the thumb has been up a while") {
    for (const common::Pending pending : kPrompts) {
        CAPTURE(pending);
        Unit unit(pending);
        unit.hold_through_the_prompt();
        unit.pass(250);
        unit.rig.press();
        unit.pass(100);
        CHECK(unit.prompt_stands());
        CHECK_FALSE(unit.allowed_anything());

        unit.pass(800);
        unit.rig.press();
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
    rig.settings().alarm = go::Alarm::Soft;
    rig.run(0, 500);
    rig.platform.board_gpio().button_down = true;
    rig.run(550, 650);
    rig.send("{\"command\":\"set\",\"alarm\":3}");
    rig.run(700, 2000);
    REQUIRE(rig.product.screen().prompt() == common::Pending::Set);
    rig.platform.board_gpio().button_down = false;
    rig.run(2050, 2050);
    rig.run(2090, 2610);
    rig.platform.board_gpio().button_down = true;
    rig.run(2650, 2690, 40);
    rig.platform.board_gpio().button_down = false;
    rig.run(2730, 2770, 40);
    rig.now_ms = 2810;
    rig.press();
    rig.run(rig.now_ms, rig.now_ms + 100);
    CHECK(rig.product.config_link().config().pending() == common::Pending::Set);
    CHECK(rig.settings().alarm == go::Alarm::Soft);
}

// Opus's review: the shutdown ignores the first hold after boot, so its release is the last one.
TEST_CASE("product: after a hold carried from boot the gesture waits a window from the release") {
    Rig rig;
    rig.platform.board_gpio().button_down = true;
    REQUIRE(rig.setup() == Status::Ok);
    rig.send("{\"command\":\"dfu\",\"version\":\"0.2.0+15\"}");
    rig.run(rig.now_ms, rig.now_ms + 2600);
    rig.now_ms += 2650;
    REQUIRE(rig.product.screen().prompt() == common::Pending::Dfu);
    REQUIRE_FALSE(rig.product.shutdown().going_down());
    rig.platform.board_gpio().button_down = false;
    rig.run(rig.now_ms, rig.now_ms + 100);
    rig.now_ms += 150;
    rig.press();
    rig.run(rig.now_ms, rig.now_ms + 100);
    rig.now_ms += 150;
    CHECK(rig.product.config_link().config().pending() == common::Pending::Dfu);
    CHECK_FALSE(rig.product.config_link().config().receiving_firmware());

    rig.run(rig.now_ms, rig.now_ms + 800);
    rig.now_ms += 850;
    rig.press();
    rig.run(rig.now_ms, rig.now_ms + 100);
    CHECK(rig.product.config_link().config().receiving_firmware());
}

// GPT's review: a phone that drops and stages the same kind of prompt within one pass replaces it.
TEST_CASE("product: a replaced prompt cannot spend a hold begun on the one it replaced") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.settings().alarm = go::Alarm::Soft;
    rig.run(0, 500);
    rig.send("{\"command\":\"set\",\"alarm\":0}");
    rig.run(550, 3500);
    REQUIRE(rig.product.screen().prompt() == common::Pending::Set);
    rig.platform.board_gpio().button_down = true;
    rig.run(3550, 3650);
    rig.drop_link();
    rig.raise_link(2);
    rig.send_from(2, "{\"command\":\"set\",\"alarm\":3}");
    rig.run(3700, 4500);
    REQUIRE(rig.product.config_link().config().claim_holder() == 2);
    REQUIRE(rig.product.screen().prompt() == common::Pending::Set);
    rig.platform.board_gpio().button_down = false;
    rig.run(4550, 4650);
    rig.now_ms = 4700;
    rig.press();
    rig.run(rig.now_ms, rig.now_ms + 100);
    CHECK(rig.product.config_link().config().pending() == common::Pending::Set);
    CHECK(rig.settings().alarm == go::Alarm::Soft);
}

// GPT's re-review: the longest gap before the accepting press is 560 ms, from 2090 to 2650.
TEST_CASE(
    "product: after a press that began before arming, the accepting press waits a quiet window") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.settings().alarm = go::Alarm::Soft;
    rig.run(0, 500);
    rig.platform.board_gpio().button_down = true;
    rig.run(550, 650);
    rig.send("{\"command\":\"set\",\"alarm\":3}");
    rig.run(700, 2000);
    REQUIRE(rig.product.screen().prompt() == common::Pending::Set);
    rig.platform.board_gpio().button_down = false;
    rig.run(2050, 2050);
    rig.run(2090, 2610);
    rig.platform.board_gpio().button_down = true;
    rig.run(2650, 2690, 40);
    rig.platform.board_gpio().button_down = false;
    rig.run(2730, 2770, 40);
    rig.now_ms = 2810;
    rig.press();
    rig.run(rig.now_ms, rig.now_ms + 100);
    CHECK(rig.product.config_link().config().pending() == common::Pending::Set);
    CHECK(rig.settings().alarm == go::Alarm::Soft);

    rig.run(rig.now_ms, rig.now_ms + 750);
    rig.now_ms += 800;
    rig.press();
    rig.run(rig.now_ms, rig.now_ms + 100);
    CHECK(rig.settings().alarm == go::Alarm::Loud);
}
