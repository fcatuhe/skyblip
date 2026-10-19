// Activation end to end on the running product: silent until the server signs it a serial.
#include <cstring>
#include <string>

#include "core/comms/config.h"
#include "core/comms/identity_link.h"
#include "core/identity/unit.h"
#include "core/util/hex.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/identity.h"
#include "hardware/platform/host/link.h"
#include "hardware/platform/host/platform.h"
#include "hardware/platform/host/watchdog.h"
#include "products/skyblip_go/pages/activation.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/activation_vectors.h"
#include "test/support/config_frame.h"
#include "test/support/glass_text.h"
#include "test/support/product_rig.h"

using namespace skyblip;
using platform::host::Identity;

namespace {

// INFO: fc 18oct26 what Chrome negotiates, the one browser that reaches the unit
constexpr uint16_t kChromePayloadBytes = 495;

constexpr ports::Capabilities without(ports::Capability part) {
    return static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(part));
}

std::string register_command(const std::string& server_nonce, const std::string& signature) {
    return "{\"cmd\":\"register\",\"server_nonce\":\"" + server_nonce + "\",\"signature\":\"" +
           signature + "\"}";
}

std::string unlock_command(long serial, const std::string& signature) {
    return "{\"cmd\":\"unlock\",\"serial\":" + std::to_string(serial) + ",\"signature\":\"" +
           signature + "\"}";
}

// INFO: fc 18oct26 a unit fresh off the line: keyed, no serial, running the vectors' 0.3.0+1234
struct Bench {
    Rig rig;
    ActivationVectors vectors;
    uint32_t t{0};

    explicit Bench(ports::Capabilities fitted = platform::host::Platform::kFullyFitted)
        : rig(fitted, std::nullopt) {
        rig.platform.dfu().running_version_valid = true;
        rig.platform.dfu().running_image_version = ports::ImageVersion{0, 3, 0, 1234};
        rig.platform.link().declare_payload_bytes(kChromePayloadBytes);
    }

    void boot() {
        REQUIRE(rig.setup() == Status::Ok);
        pass(500);
    }

    void pass(uint32_t ms) {
        rig.run(t, t + ms);
        t += ms + 50;
    }

    std::string say(const std::string& json) {
        const size_t before = rig.platform.link().sent.size();
        rig.send(json.c_str());
        pass(300);
        return answer_since(before);
    }

    std::string answer_since(size_t before) {
        const auto& sent = rig.platform.link().sent;
        std::string answer;
        for (size_t i = before; i < sent.size(); i++)
            if (sent[i].endpoint == events::Endpoint::Config &&
                sent[i].bytes.rfind("{\"cmd\":\"status\"", 0) != 0)
                answer = sent[i].bytes;
        return answer;
    }

    std::string challenge_with_vectors_nonce() {
        rig.platform.identity().queue_random(vectors.bytes<16>("unit_nonce"));
        return say("{\"cmd\":\"challenge\"}");
    }

    std::string register_with_vectors() {
        return say(
            register_command(vectors.text("server_nonce"), vectors.text("register_signature")));
    }

    std::string unlock_with_vectors() {
        return say(unlock_command(vectors.number("serial"), vectors.text("unlock_signature")));
    }

    // INFO: fc 18oct26 a press counts once the prompt is on the glass and the thumb has stopped
    void read_the_prompt() { pass(3000); }

    std::string press_twice() {
        read_the_prompt();
        const size_t before = rig.platform.link().sent.size();
        rig.double_press(t);
        pass(500);
        return answer_since(before);
    }

    comms::Pending pending() { return rig.product.config_link().config().pending(); }
    const identity::Activation& activation() { return rig.state().activation; }
};

std::string refusal(const char* word) {
    return std::string("{\"ack\":false,\"reason\":\"") + word + "\"}";
}

}  // namespace

// Issue #126: the first of three refusals, each with its own case.
TEST_CASE("activation: a unit with no serial arms neither receiver nor transmitter, fix or not") {
    Bench bench;
    bench.boot();
    bench.rig.seconds(bench.t, 20, 0, 300);
    CHECK_FALSE(bench.activation().activated());
    CHECK(bench.rig.product.radio().arm_count() == 0);
    CHECK(bench.rig.product.radio().armed_mode() == ports::RfMode::Idle);
    CHECK(bench.rig.state().air.tx_ok == 0);
    CHECK(bench.rig.product.radio().transmitter().sent_count() == 0);
}

TEST_CASE("activation: a unit with no serial advertises activate and its tag, and says where") {
    Bench bench;
    bench.boot();
    CHECK(bench.rig.platform.link().advertised == "activate 3F2A skyBlip");
    const go::Glass& glass = bench.rig.product.screen().framebuffer();
    CHECK(reads_in(glass, go::kActivationWhere, 0, 0, go::kGlassWidth, go::kGlassHeight));
    CHECK(reads_in(glass, "3F2A", 0, go::kActivationTagY - 1, go::kGlassWidth, go::kGlassHeight,
                   go::kActivationTagScale));
    CHECK(reads_in(glass, "ACTIVATE 3F2A SKYBLIP", 0, 0, go::kGlassWidth, go::kGlassHeight));
}

// The server's update page reads 'address' in the reply as activated.
TEST_CASE("activation: get leaves address out until there is one, keeps address_mapping_table") {
    Bench bench;
    bench.boot();
    const std::string reply = bench.say("{\"cmd\":\"get\"}");
    CHECK(reply.find("\"address\"") == std::string::npos);
    CHECK(reply.find("\"address_mapping_table\":58") != std::string::npos);
}

// The vectors replayed through the product: every frame the unit says is the server's.
TEST_CASE("activation: the vectors' exchange activates the unit, byte for byte") {
    Bench bench;
    REQUIRE(bench.vectors.flag("release"));
    bench.rig.platform.dfu().release = true;
    bench.boot();

    CHECK(bench.challenge_with_vectors_nonce() ==
          "{\"cmd\":\"challenge\",\"nonce\":\"" + bench.vectors.text("unit_nonce") +
              "\",\"server_key_id\":\"" + bench.vectors.text("server_key_id") + "\"}");

    CHECK(bench.register_with_vectors() ==
          "{\"ack\":false,\"pending\":true,\"reason\":\"confirm_register\"}");
    REQUIRE(bench.pending() == comms::Pending::Register);

    const std::string registration = bench.press_twice();
    CHECK(registration ==
          "{\"cmd\":\"registration\",\"device_id\":\"" + bench.vectors.text("device_id") +
              "\",\"public_key\":\"" + bench.vectors.text("unit_public_key") + "\",\"version\":\"" +
              bench.vectors.text("version") + "\",\"server_key_id\":\"" +
              bench.vectors.text("server_key_id") + "\",\"release\":true,\"signature\":\"" +
              bench.vectors.text("registration_signature") + "\"}");
    CHECK_FALSE(bench.activation().activated());

    CHECK(bench.unlock_with_vectors() == "{\"ack\":true,\"reason\":\"unlock\"}");
    CHECK(bench.activation().serial == 298);
    CHECK(bench.activation().address() == settings::AirAddress{0x00012Au});
    CHECK(bench.rig.platform.identity().writes == 1);
    REQUIRE(bench.rig.platform.identity().stored_unlock_code);
    CHECK(bench.rig.platform.identity().stored_unlock_code->serial == 298);
}

TEST_CASE("activation: once unlocked the unit names itself, reports its address, arms its radio") {
    Bench bench;
    bench.boot();
    bench.challenge_with_vectors_nonce();
    bench.register_with_vectors();
    bench.press_twice();
    REQUIRE(bench.unlock_with_vectors() == "{\"ack\":true,\"reason\":\"unlock\"}");
    bench.pass(3000);

    CHECK(bench.rig.platform.link().advertised == "skyBlip Go+ 00012A");
    CHECK(bench.say("{\"cmd\":\"get\"}").find("\"address\":298") != std::string::npos);
    CHECK(bench.rig.product.radio().arm_count() > 0);
    CHECK_FALSE(reads_in(bench.rig.product.screen().framebuffer(), go::kActivationWhere, 0, 0,
                         go::kGlassWidth, go::kGlassHeight));
}

TEST_CASE("activation: the glass names the operation two presses authorise") {
    Bench bench;
    bench.boot();
    bench.challenge_with_vectors_nonce();
    bench.register_with_vectors();
    bench.read_the_prompt();
    REQUIRE(bench.rig.product.screen().prompt() == comms::Pending::Register);
    const go::Glass& glass = bench.rig.product.screen().framebuffer();
    CHECK(reads_in(glass, comms::pending_title(comms::Pending::Register), 0, 0, go::kGlassWidth,
                   go::kGlassHeight, 2));
    CHECK(reads_in(glass, "SKYBLIP.EU", 0, 0, go::kGlassWidth, go::kGlassHeight));
}

TEST_CASE("activation: one press cancels the registration, thirty seconds expire it") {
    SUBCASE("one press") {
        Bench bench;
        bench.boot();
        bench.challenge_with_vectors_nonce();
        bench.register_with_vectors();
        bench.read_the_prompt();
        const size_t before = bench.rig.platform.link().sent.size();
        bench.rig.press(bench.t);
        bench.pass(1000);
        CHECK(bench.answer_since(before) == refusal("cancelled"));
    }
    SUBCASE("no press") {
        Bench bench;
        bench.boot();
        bench.challenge_with_vectors_nonce();
        bench.register_with_vectors();
        const size_t before = bench.rig.platform.link().sent.size();
        bench.pass(comms::kPromptForgetMs + 500);
        CHECK(bench.answer_since(before) == refusal("expired"));
    }
}

TEST_CASE("activation: register and unlock without a challenge have no session to sign against") {
    Bench bench;
    bench.boot();
    CHECK(bench.register_with_vectors() == refusal("no_session"));
    CHECK(bench.unlock_with_vectors() == refusal("no_session"));
    CHECK(bench.pending() == comms::Pending::None);
}

TEST_CASE("activation: a register the trusted server did not sign is refused at the door") {
    Bench bench;
    bench.boot();
    bench.challenge_with_vectors_nonce();
    std::string forged = bench.vectors.text("register_signature");
    forged[0] = forged[0] == '0' ? '1' : '0';
    CHECK(bench.say(register_command(bench.vectors.text("server_nonce"), forged)) ==
          refusal("bad_signature"));
    CHECK(bench.say(register_command("not hex", bench.vectors.text("register_signature"))) ==
          refusal("bad_signature"));
    CHECK(bench.pending() == comms::Pending::None);
}

// A fresh challenge replaces Nu, so R over the old one no longer checks.
TEST_CASE("activation: a second challenge replaces the nonce, and R over the first is refused") {
    Bench bench;
    bench.boot();
    bench.challenge_with_vectors_nonce();
    bench.say("{\"cmd\":\"challenge\"}");
    CHECK(bench.register_with_vectors() == refusal("bad_signature"));
}

// Chrome negotiates 495 bytes and an iPhone 182: the frame cannot travel on the second.
TEST_CASE(
    "activation: a link that cannot carry the registration frame is told so before the prompt") {
    Bench bench;
    bench.boot();
    bench.rig.platform.link().declare_payload_bytes(comms::kSmallestSupportedPayloadBytes);
    bench.challenge_with_vectors_nonce();
    CHECK(bench.register_with_vectors() == refusal("small_mtu"));
    CHECK(bench.pending() == comms::Pending::None);
}

TEST_CASE("activation: an unlock code that does not verify spends the nonce all the same") {
    Bench bench;
    bench.boot();
    bench.challenge_with_vectors_nonce();
    CHECK(bench.say(unlock_command(299, bench.vectors.text("unlock_signature"))) ==
          refusal("bad_signature"));
    CHECK(bench.unlock_with_vectors() == refusal("no_session"));
    CHECK_FALSE(bench.activation().activated());
    CHECK(bench.rig.platform.identity().writes == 0);
}

TEST_CASE("activation: an identity page that will not take the code is refused as flash") {
    Bench bench;
    bench.boot();
    bench.rig.platform.identity().write_fails = true;
    bench.challenge_with_vectors_nonce();
    CHECK(bench.unlock_with_vectors() == refusal("flash"));
    CHECK_FALSE(bench.activation().activated());
}

TEST_CASE("activation: the nonce dies with the link that holds the claim, and after ten minutes") {
    SUBCASE("the link drops") {
        Bench bench;
        bench.boot();
        bench.challenge_with_vectors_nonce();
        bench.rig.drop_link();
        bench.pass(200);
        CHECK(bench.unlock_with_vectors() == refusal("no_session"));
    }
    SUBCASE("ten minutes pass") {
        Bench bench;
        bench.boot();
        bench.challenge_with_vectors_nonce();
        bench.pass(comms::IdentityLink::kNonceForgetMs);
        CHECK(bench.unlock_with_vectors() == refusal("no_session"));
    }
}

TEST_CASE("activation: a unit with no key, or no version to report, cannot be activated") {
    SUBCASE("no key") {
        Bench bench;
        bench.rig.platform.identity().key_provisioned = false;
        bench.boot();
        CHECK(bench.say("{\"cmd\":\"challenge\"}") == refusal("keyless"));
    }
    SUBCASE("no randomness") {
        Bench bench;
        bench.boot();
        bench.rig.platform.identity().random_fails = true;
        CHECK(bench.say("{\"cmd\":\"challenge\"}") == refusal("keyless"));
    }
    SUBCASE("no running version") {
        Bench bench;
        bench.rig.platform.dfu().running_version_valid = false;
        bench.boot();
        CHECK(bench.say("{\"cmd\":\"challenge\"}") == refusal("keyless"));
    }
    SUBCASE("a version lost between the challenge and the register") {
        Bench bench;
        bench.boot();
        bench.challenge_with_vectors_nonce();
        bench.rig.platform.dfu().running_version_valid = false;
        CHECK(bench.register_with_vectors() == refusal("keyless"));
    }
}

// Issue #126: the barometer is required, so a unit without one is refused and serves nothing.
TEST_CASE("activation: a unit with no barometer neither flies, nor advertises, nor answers") {
    Bench bench(without(ports::Capability::Barometer));
    REQUIRE(bench.rig.setup() == Status::Down);
    CHECK(bench.say("{\"cmd\":\"challenge\"}").empty());
    bench.pass(3000);
    CHECK(bench.rig.product.radio().arm_count() == 0);
    CHECK_FALSE(bench.activation().activated());
    CHECK(bench.rig.platform.link().advertise_calls == 0);
    CHECK(bench.rig.platform.link().advertised.empty());
}

TEST_CASE("activation: a link that refuses the first name is asked again, once a second") {
    Rig rig;
    rig.platform.link().advertise_refusals = 2;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.platform.link().advertised.empty());
    rig.run(0, 900);
    CHECK(rig.platform.link().advertise_calls == 1);
    rig.run(950, 2100);
    CHECK(rig.platform.link().advertised == "skyBlip Go+ 005AFE");
    CHECK(rig.platform.link().advertise_calls == 3);
    rig.run(2150, 5000);
    CHECK(rig.platform.link().advertise_calls == 3);
}

// A Bluetooth that failed to come up refuses forever, and must not flood the console doing it.
TEST_CASE("activation: a link that never takes the name is asked at most once a second") {
    Rig rig;
    rig.platform.link().advertise_refusals = 1000;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 10000);
    CHECK(rig.platform.link().advertise_calls <= 11);
    CHECK(rig.platform.link().advertise_calls >= 10);
}

// The activation page has nothing behind it: a press opens no menu and a tap walks no page.
TEST_CASE(
    "activation: an unactivated unit drops a press and a tap, and still powers off on a hold") {
    Bench bench;
    bench.boot();
    const go::Settings before = bench.rig.settings();
    bench.rig.tap(bench.t);
    bench.rig.press(bench.t);
    bench.rig.long_touch(bench.t);
    bench.pass(1000);
    CHECK(bench.rig.product.screen().mode() == go::Mode::Page);
    CHECK(bench.rig.product.screen().page() == go::Page::Radar);
    CHECK(std::memcmp(&bench.rig.settings(), &before, sizeof(go::Settings)) == 0);
    CHECK(reads_in(bench.rig.product.screen().framebuffer(), go::kActivationWhere, 0, 0,
                   go::kGlassWidth, go::kGlassHeight));

    bench.rig.platform.board_gpio().button_down = true;
    bench.pass(power::kLongPressHoldMs + 1000);
    CHECK(bench.rig.product.shutdown().going_down());
    CHECK(bench.rig.product.shutdown().reason() == power::ShutdownReason::LongPress);
}

TEST_CASE("activation: the self-test page shows the address an unlock gave, without a reboot") {
    Bench bench;
    bench.boot();
    bench.challenge_with_vectors_nonce();
    bench.register_with_vectors();
    bench.press_twice();
    REQUIRE(bench.unlock_with_vectors() == "{\"ack\":true,\"reason\":\"unlock\"}");
    bench.pass(3000);
    bench.rig.show(bench.t, go::Page::SelfTest);
    bench.pass(2000);
    REQUIRE(bench.rig.product.screen().page() == go::Page::SelfTest);
    CHECK(
        reads_in(bench.rig.product.screen().framebuffer(), "ID 00012A", 0, 0, go::kGlassWidth, 40));
}

namespace {

// INFO: fc 18oct26 an activated unit on the ground, with an unlock it holds for a write window
struct QueuedUnlock {
    Bench bench{platform::host::Platform::kFullyFitted};
    size_t before{0};
    static constexpr uint16_t kNewSerial = 0x0101;

    QueuedUnlock() {
        bench.rig.platform.identity().stored_unlock_code =
            Identity::unlock_code_for(Identity::kSerial, identity::Nonce{});
        bench.boot();
        REQUIRE(bench.activation().serial == Identity::kSerial);
        bench.rig.seconds(bench.t, 20, 0, 300);
        REQUIRE(bench.rig.product.config_link().config().flight_state() ==
                flight::FlightState::Ground);

        const identity::Nonce nonce{4, 2};
        bench.rig.platform.identity().queue_random(nonce);
        REQUIRE(bench.say("{\"cmd\":\"challenge\"}").find("\"nonce\"") != std::string::npos);
        const identity::UnlockCode code = Identity::unlock_code_for(kNewSerial, nonce);
        std::string signature(128, '?');
        hex::write(ConstByteSpan(code.signature.data(), code.signature.size()), signature.data());
        before = bench.rig.platform.link().sent.size();
        bench.rig.send(unlock_command(kNewSerial, signature).c_str());
        bench.pass(100);
        REQUIRE(bench.answer_since(before).empty());
        REQUIRE(bench.rig.platform.identity().writes == 0);
    }

    int unlock_answers() {
        int n = 0;
        const auto& sent = bench.rig.platform.link().sent;
        for (size_t i = before; i < sent.size(); i++)
            if (sent[i].endpoint == events::Endpoint::Config &&
                sent[i].bytes.find("\"reason\"") != std::string::npos)
                n++;
        return n;
    }
};

}  // namespace

// The radio of an activated unit is running, so its unlock page is placed outside a dwell, later.
TEST_CASE(
    "activation: an activated unit on the ground answers an unlock once the write is placed") {
    QueuedUnlock q;
    q.bench.rig.seconds(q.bench.t, 4, 0, 300);
    CHECK(q.bench.answer_since(q.before) == "{\"ack\":true,\"reason\":\"unlock\"}");
    CHECK(q.bench.rig.platform.identity().writes == 1);
    CHECK(q.bench.activation().serial == QueuedUnlock::kNewSerial);
}

// A held unlock is never forced onto a cell the settings writer would not touch.
TEST_CASE("activation: a supply warning before the write window drops the unlock as low_power") {
    QueuedUnlock q;
    q.bench.rig.platform.system_power().supply_warning = true;
    q.bench.rig.seconds(q.bench.t, 4, 0, 300);
    CHECK(q.bench.answer_since(q.before) == refusal("low_power"));
    CHECK(q.bench.rig.platform.identity().writes == 0);
    CHECK(q.bench.activation().serial == Identity::kSerial);
}

// The answer belongs to the session that asked: another app claiming the link must not receive it.
TEST_CASE(
    "activation: a held unlock dies with the link that sent it, and nobody else is answered") {
    QueuedUnlock q;
    const uint16_t sender = q.bench.rig.platform.link().session_id();
    q.bench.rig.drop_link(sender);
    q.bench.pass(100);
    q.bench.rig.raise_link(sender + 1);
    q.bench.say("{\"cmd\":\"get\"}");
    q.bench.rig.seconds(q.bench.t, 4, 0, 300);
    CHECK(q.unlock_answers() == 0);
    CHECK(q.bench.rig.platform.identity().writes == 0);
    CHECK(q.bench.activation().serial == Identity::kSerial);
}

TEST_CASE("activation: the name is Go+ with inclinometer, buzzer and haptic fitted, Go without") {
    SUBCASE("all three fitted") {
        Rig rig;
        REQUIRE(rig.setup() == Status::Ok);
        CHECK(rig.platform.link().advertised == "skyBlip Go+ 005AFE");
    }
    SUBCASE("a Plus whose hub has no image to boot is still fitted") {
        Rig rig;
        rig.platform.set_imu_firmware_linked(false);
        REQUIRE(rig.setup() == Status::Ok);
        CHECK(rig.platform.link().advertised == "skyBlip Go+ 005AFE");
    }
    SUBCASE("no haptic") {
        Rig rig(without(ports::Capability::Haptic));
        REQUIRE(rig.setup() == Status::Ok);
        CHECK(rig.platform.link().advertised == "skyBlip Go 005AFE");
    }
    SUBCASE("no buzzer") {
        Rig rig(without(ports::Capability::Buzzer));
        REQUIRE(rig.setup() == Status::Ok);
        CHECK(rig.platform.link().advertised == "skyBlip Go 005AFE");
    }
}

namespace {

std::string hex_text(ConstByteSpan bytes) {
    std::string out(2 * bytes.size(), '?');
    hex::write(bytes, out.data());
    return out;
}

// INFO: fc 18oct26 the config endpoint alone, for the flight states the product cannot be put in
struct Desk {
    platform::host::Link link;
    platform::host::Watchdog watchdog;
    platform::host::Dfu dfu{watchdog};
    go::Settings settings = go::defaults();
    go::SettingsStore store{settings, kTestActivation};
    comms::ConfigService config{link, store, &dfu};
    Identity port;
    identity::Unit unit{port};
    comms::IdentityLink identity{unit};
    ActivationVectors vectors;

    explicit Desk(std::optional<uint16_t> serial = std::nullopt) : port(serial) {
        link.declare_payload_bytes(kChromePayloadBytes);
        link.raise_link(1);
        dfu.running_version_valid = true;
        dfu.running_image_version = ports::ImageVersion{0, 3, 0, 1234};
        unit.load();
        config.set_identity(&identity);
    }

    std::string say(const std::string& json) {
        config.on_rx(config_frame(json.c_str()));
        return link.sent.empty() ? std::string() : link.last().bytes;
    }

    std::string register_signed_by_the_test_server() {
        const identity::Nonce server_nonce{9};
        const crypto::Ed25519::Signature r = crypto::Ed25519::sign(
            Identity::kTestServerSeed,
            identity::register_message(identity.unit_nonce(), server_nonce).bytes());
        return say(register_command(hex_text(ConstByteSpan(server_nonce.data(), 16)),
                                    hex_text(ConstByteSpan(r.data(), r.size()))));
    }

    std::string unlock_signed_by_the_test_server(uint16_t serial) {
        const identity::UnlockCode code = Identity::unlock_code_for(serial, identity.unit_nonce());
        return say(unlock_command(
            serial, hex_text(ConstByteSpan(code.signature.data(), code.signature.size()))));
    }
};

}  // namespace

// A bench indoors has no fix, and an unactivated unit is silent: no ground gate is owed.
TEST_CASE(
    "activation: an unactivated unit confirms its register prompt whatever the flight state") {
    for (flight::FlightState fs : {flight::FlightState::Unknown, flight::FlightState::Airborne}) {
        CAPTURE(static_cast<int>(fs));
        Desk d;
        d.dfu.release = d.vectors.flag("release");
        d.config.set_flight_state(fs);
        d.port.queue_random(d.vectors.bytes<16>("unit_nonce"));
        d.say("{\"cmd\":\"challenge\"}");
        d.say(
            register_command(d.vectors.text("server_nonce"), d.vectors.text("register_signature")));
        REQUIRE(d.config.pending() == comms::Pending::Register);
        d.config.set_flight_state(fs);
        REQUIRE(d.config.pending() == comms::Pending::Register);
        d.config.confirm();
        CHECK(d.link.last().bytes.find(d.vectors.text("registration_signature")) !=
              std::string::npos);
    }
}

// An activated unit transmits, so it owes every identity command the ground gate the others obey.
TEST_CASE("activation: an activated unit refuses challenge, register and unlock off the ground") {
    for (flight::FlightState fs : {flight::FlightState::Unknown, flight::FlightState::Airborne}) {
        CAPTURE(static_cast<int>(fs));
        Desk d(Identity::kSerial);
        REQUIRE(d.identity.activated());
        d.config.set_flight_state(fs);
        CHECK(d.say("{\"cmd\":\"challenge\"}") == refusal("in_flight"));
        CHECK(d.say(register_command(d.vectors.text("server_nonce"),
                                     d.vectors.text("register_signature"))) ==
              refusal("in_flight"));
        CHECK(d.say(unlock_command(298, d.vectors.text("unlock_signature"))) ==
              refusal("in_flight"));
    }
}

TEST_CASE("activation: an activated unit's register prompt is cancelled at take-off") {
    Desk d(Identity::kSerial);
    d.config.set_flight_state(flight::FlightState::Ground);
    REQUIRE(d.say("{\"cmd\":\"challenge\"}").find("\"nonce\"") != std::string::npos);
    d.register_signed_by_the_test_server();
    REQUIRE(d.config.pending() == comms::Pending::Register);
    d.config.set_flight_state(flight::FlightState::Airborne);
    CHECK(d.config.pending() == comms::Pending::None);
    CHECK(d.link.last().bytes == refusal("in_flight"));
}

TEST_CASE("activation: a cell too low to write settings refuses an unlock and keeps the nonce") {
    Desk d;
    d.say("{\"cmd\":\"challenge\"}");
    d.config.set_supply_warned(true);
    CHECK(d.unlock_signed_by_the_test_server(0x0042) == refusal("low_power"));
    CHECK(d.port.writes == 0);
    d.config.set_supply_warned(false);
    CHECK(d.unlock_signed_by_the_test_server(0x0042) == "{\"ack\":true,\"reason\":\"unlock\"}");
}

// The server assigns from 1: serial 0 would put 0x000000, read as no address, on the air.
TEST_CASE("activation: an unlock code for serial 0 is refused, however well it is signed") {
    Desk d;
    d.say("{\"cmd\":\"challenge\"}");
    CHECK(d.unlock_signed_by_the_test_server(0) == refusal("bad_signature"));
    CHECK(d.port.writes == 0);
    CHECK_FALSE(d.identity.activated());
}

// The nonce has a deadline, not a sweep: the pass that reaches it already finds it dead.
TEST_CASE("activation: a nonce lives kNonceForgetMs after its challenge, then is dead") {
    constexpr uint32_t kChallengeMs = 5000;
    for (uint32_t after :
         {comms::IdentityLink::kNonceForgetMs - 1, comms::IdentityLink::kNonceForgetMs,
          comms::IdentityLink::kNonceForgetMs + 1}) {
        CAPTURE(after);
        Desk d;
        d.config.tick(kChallengeMs);
        d.say("{\"cmd\":\"challenge\"}");
        d.config.tick(kChallengeMs + after);
        const std::string answer = d.unlock_signed_by_the_test_server(0x0042);
        if (after < comms::IdentityLink::kNonceForgetMs)
            CHECK(answer == "{\"ack\":true,\"reason\":\"unlock\"}");
        else
            CHECK(answer == refusal("no_session"));
    }
}

// 2^32 ms after a challenge, millis() is inside the window again: the sweep must have closed it.
TEST_CASE("activation: an expired nonce stays dead when millis() wraps back into its window") {
    constexpr uint32_t kChallengeMs = 0xFFFF0000u;
    Desk d;
    d.config.tick(kChallengeMs);
    d.say("{\"cmd\":\"challenge\"}");
    d.config.tick(kChallengeMs + comms::IdentityLink::kNonceForgetMs);
    d.config.tick(kChallengeMs + 5);
    CHECK(d.unlock_signed_by_the_test_server(0x0042) == refusal("no_session"));
    CHECK(d.port.writes == 0);
}

// The server keeps the release flag beside the key, and the unit's S covers it either way.
TEST_CASE("activation: the registration says whether the build is a release, and signs it") {
    for (bool release : {false, true}) {
        CAPTURE(release);
        Desk d;
        d.dfu.release = release;
        d.say("{\"cmd\":\"challenge\"}");
        d.register_signed_by_the_test_server();
        REQUIRE(d.config.pending() == comms::Pending::Register);
        d.config.confirm();
        const std::string frame = d.link.last().bytes;
        CHECK(frame.find(release ? "\"release\":true," : "\"release\":false,") !=
              std::string::npos);
        CHECK(frame.find("\"server_key_id\":\"890026b1\",\"release\"") != std::string::npos);

        const identity::Nonce server_nonce{9};
        const identity::Message signed_over = identity::registration_message(
            Identity::kDeviceId, crypto::Ed25519::public_key_of(Identity::kUnitSeed),
            d.identity.unit_nonce(), server_nonce, d.unit.server_key_id(), release, "0.3.0+1234");
        crypto::Ed25519::Signature s{};
        const size_t at = frame.find("\"signature\":\"") + 13;
        REQUIRE(hex::read(frame.substr(at, 128).c_str(), ByteSpan(s.data(), s.size())));
        CHECK(crypto::Ed25519::verify(s, crypto::Ed25519::public_key_of(Identity::kUnitSeed),
                                      signed_over.bytes()));
    }
}

// The widest version and release false make the widest frame, the size register checks.
TEST_CASE("activation: the registration frame at the widest version is exactly the size checked") {
    Desk d;
    d.dfu.running_image_version = dfu::kWidestVersion;
    d.say("{\"cmd\":\"challenge\"}");
    const identity::Nonce unit_nonce = d.identity.unit_nonce();
    char nonce_hex[33] = {0};
    hex::write(ConstByteSpan(unit_nonce.data(), unit_nonce.size()), nonce_hex);
    const identity::Nonce server_nonce{7};
    char server_hex[33] = {0};
    hex::write(ConstByteSpan(server_nonce.data(), server_nonce.size()), server_hex);
    const crypto::Ed25519::Signature r = crypto::Ed25519::sign(
        Identity::kTestServerSeed, identity::register_message(unit_nonce, server_nonce).bytes());
    char r_hex[129] = {0};
    hex::write(ConstByteSpan(r.data(), r.size()), r_hex);
    d.say(register_command(server_hex, r_hex));
    REQUIRE(d.config.pending() == comms::Pending::Register);
    d.config.confirm();
    CHECK(d.link.last().bytes.size() == size_t(comms::IdentityLink::kRegistrationFrameBytes));
    CHECK(d.link.last().bytes.find("9.99.99+99999") != std::string::npos);
    CHECK(d.link.last().bytes.find("\"release\":false") != std::string::npos);
    CHECK(comms::IdentityLink::kRegistrationFrameBytes <= kChromePayloadBytes);
}
