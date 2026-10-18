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
    ActivationVectors v;
    uint32_t t{0};

    explicit Bench(ports::Capabilities fitted = platform::host::Platform::kFullyFitted)
        : rig(fitted, std::nullopt) {
        rig.platform.dfu().has_running = true;
        rig.platform.dfu().running = ports::ImageVersion{0, 3, 0, 1234};
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

    std::string hello_with_vectors_nonce() {
        rig.platform.identity().queue_random(v.bytes<16>("unit_nonce"));
        return say("{\"cmd\":\"hello\"}");
    }

    std::string register_with_vectors() {
        return say(register_command(v.text("server_nonce"), v.text("register_signature")));
    }

    std::string unlock_with_vectors() {
        return say(unlock_command(v.number("serial"), v.text("unlock_signature")));
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

    comms::Pending pending() { return rig.product.config().config().pending(); }
    const identity::Activation& activation() { return rig.state().activation; }
};

std::string refusal(const char* word) {
    return std::string("{\"ack\":false,\"reason\":\"") + word + "\"}";
}

}  // namespace

// Issue #126: the first of three refusals, each with its own case.
TEST_CASE("activation: a unit with no serial arms neither receiver nor transmitter, fix or not") {
    Bench b;
    b.boot();
    b.rig.seconds(b.t, 20, 0, 300);
    CHECK_FALSE(b.activation().activated());
    CHECK(b.rig.product.radio().arm_count() == 0);
    CHECK(b.rig.product.radio().armed_mode() == ports::RfMode::Idle);
    CHECK(b.rig.state().air.tx_ok == 0);
    CHECK(b.rig.product.radio().transmitter().sent_count() == 0);
}

TEST_CASE("activation: a unit with no serial advertises activate and its tag, and says where") {
    Bench b;
    b.boot();
    CHECK(b.rig.platform.link().advertised == "activate 3F2A skyBlip");
    const go::Glass& glass = b.rig.product.screen().framebuffer();
    CHECK(reads_in(glass, go::kActivationWhere, 0, 0, go::kGlassW, go::kGlassH));
    CHECK(reads_in(glass, "3F2A", 0, go::kActivationTagY - 1, go::kGlassW, go::kGlassH,
                   go::kActivationTagScale));
    CHECK(reads_in(glass, "ACTIVATE 3F2A SKYBLIP", 0, 0, go::kGlassW, go::kGlassH));
}

// The server's update page reads 'addr' in the reply as activated.
TEST_CASE("activation: get leaves addr out until there is one, and keeps addr_table") {
    Bench b;
    b.boot();
    const std::string reply = b.say("{\"cmd\":\"get\"}");
    CHECK(reply.find("\"addr\"") == std::string::npos);
    CHECK(reply.find("\"addr_table\":58") != std::string::npos);
}

// The vectors replayed through the product: every frame the unit says is the server's.
TEST_CASE("activation: the vectors' exchange activates the unit, byte for byte") {
    Bench b;
    b.boot();

    CHECK(b.hello_with_vectors_nonce() == "{\"cmd\":\"hello\",\"nonce\":\"" +
                                              b.v.text("unit_nonce") + "\",\"server_key\":\"" +
                                              b.v.text("server_key") + "\"}");

    CHECK(b.register_with_vectors() ==
          "{\"ack\":false,\"pending\":true,\"reason\":\"confirm_register\"}");
    REQUIRE(b.pending() == comms::Pending::Register);

    const std::string registration = b.press_twice();
    CHECK(registration == "{\"cmd\":\"registration\",\"device_id\":\"" + b.v.text("device_id") +
                              "\",\"public_key\":\"" + b.v.text("unit_public_key") +
                              "\",\"version\":\"" + b.v.text("version") + "\",\"server_key\":\"" +
                              b.v.text("server_key") + "\",\"signature\":\"" +
                              b.v.text("registration_signature") + "\"}");
    CHECK_FALSE(b.activation().activated());

    CHECK(b.unlock_with_vectors() == "{\"ack\":true,\"reason\":\"unlock\"}");
    CHECK(b.activation().serial == 298);
    CHECK(b.activation().address() == settings::AirAddress{0x00012Au});
    CHECK(b.rig.platform.identity().writes == 1);
    REQUIRE(b.rig.platform.identity().stored);
    CHECK(b.rig.platform.identity().stored->serial == 298);
}

TEST_CASE("activation: once unlocked the unit names itself, reports its addr and arms its radio") {
    Bench b;
    b.boot();
    b.hello_with_vectors_nonce();
    b.register_with_vectors();
    b.press_twice();
    REQUIRE(b.unlock_with_vectors() == "{\"ack\":true,\"reason\":\"unlock\"}");
    b.pass(3000);

    CHECK(b.rig.platform.link().advertised == "skyBlip Go+ 00012A");
    CHECK(b.say("{\"cmd\":\"get\"}").find("\"addr\":298") != std::string::npos);
    CHECK(b.rig.product.radio().arm_count() > 0);
    CHECK_FALSE(reads_in(b.rig.product.screen().framebuffer(), go::kActivationWhere, 0, 0,
                         go::kGlassW, go::kGlassH));
}

TEST_CASE("activation: the glass names the operation two presses authorise") {
    Bench b;
    b.boot();
    b.hello_with_vectors_nonce();
    b.register_with_vectors();
    b.read_the_prompt();
    REQUIRE(b.rig.product.screen().prompt() == comms::Pending::Register);
    const go::Glass& glass = b.rig.product.screen().framebuffer();
    CHECK(reads_in(glass, comms::pending_title(comms::Pending::Register), 0, 0, go::kGlassW,
                   go::kGlassH, 2));
    CHECK(reads_in(glass, "SKYBLIP.EU", 0, 0, go::kGlassW, go::kGlassH));
}

TEST_CASE("activation: one press cancels the registration, thirty seconds expire it") {
    SUBCASE("one press") {
        Bench b;
        b.boot();
        b.hello_with_vectors_nonce();
        b.register_with_vectors();
        b.read_the_prompt();
        const size_t before = b.rig.platform.link().sent.size();
        b.rig.press(b.t);
        b.pass(1000);
        CHECK(b.answer_since(before) == refusal("cancelled"));
    }
    SUBCASE("no press") {
        Bench b;
        b.boot();
        b.hello_with_vectors_nonce();
        b.register_with_vectors();
        const size_t before = b.rig.platform.link().sent.size();
        b.pass(comms::kConfirmWindowMs + 500);
        CHECK(b.answer_since(before) == refusal("expired"));
    }
}

TEST_CASE("activation: register and unlock without a hello have no session to sign against") {
    Bench b;
    b.boot();
    CHECK(b.register_with_vectors() == refusal("no_session"));
    CHECK(b.unlock_with_vectors() == refusal("no_session"));
    CHECK(b.pending() == comms::Pending::None);
}

TEST_CASE("activation: a register the trusted server did not sign is refused at the door") {
    Bench b;
    b.boot();
    b.hello_with_vectors_nonce();
    std::string forged = b.v.text("register_signature");
    forged[0] = forged[0] == '0' ? '1' : '0';
    CHECK(b.say(register_command(b.v.text("server_nonce"), forged)) == refusal("bad_signature"));
    CHECK(b.say(register_command("not hex", b.v.text("register_signature"))) ==
          refusal("bad_signature"));
    CHECK(b.pending() == comms::Pending::None);
}

// A fresh hello replaces Nu, so R over the old one no longer checks.
TEST_CASE("activation: a second hello replaces the nonce, and R over the first is refused") {
    Bench b;
    b.boot();
    b.hello_with_vectors_nonce();
    b.say("{\"cmd\":\"hello\"}");
    CHECK(b.register_with_vectors() == refusal("bad_signature"));
}

// Chrome negotiates 495 bytes and an iPhone 182: the frame cannot travel on the second.
TEST_CASE(
    "activation: a link that cannot carry the registration frame is told so before the prompt") {
    Bench b;
    b.boot();
    b.rig.platform.link().declare_payload_bytes(comms::kSmallestSupportedPayload);
    b.hello_with_vectors_nonce();
    CHECK(b.register_with_vectors() == refusal("small_mtu"));
    CHECK(b.pending() == comms::Pending::None);
}

TEST_CASE("activation: an unlock code that does not verify spends the nonce all the same") {
    Bench b;
    b.boot();
    b.hello_with_vectors_nonce();
    CHECK(b.say(unlock_command(299, b.v.text("unlock_signature"))) == refusal("bad_signature"));
    CHECK(b.unlock_with_vectors() == refusal("no_session"));
    CHECK_FALSE(b.activation().activated());
    CHECK(b.rig.platform.identity().writes == 0);
}

TEST_CASE("activation: an identity page that will not take the code is refused as flash") {
    Bench b;
    b.boot();
    b.rig.platform.identity().write_fails = true;
    b.hello_with_vectors_nonce();
    CHECK(b.unlock_with_vectors() == refusal("flash"));
    CHECK_FALSE(b.activation().activated());
}

TEST_CASE("activation: the nonce dies with the link that holds the claim, and after ten minutes") {
    SUBCASE("the link drops") {
        Bench b;
        b.boot();
        b.hello_with_vectors_nonce();
        b.rig.drop_link();
        b.pass(200);
        CHECK(b.unlock_with_vectors() == refusal("no_session"));
    }
    SUBCASE("ten minutes pass") {
        Bench b;
        b.boot();
        b.hello_with_vectors_nonce();
        b.pass(comms::IdentityLink::kNonceForgetMs);
        CHECK(b.unlock_with_vectors() == refusal("no_session"));
    }
}

TEST_CASE("activation: a unit with no key, or no version to report, cannot be activated") {
    SUBCASE("no key") {
        Bench b;
        b.rig.platform.identity().keyed = false;
        b.boot();
        CHECK(b.say("{\"cmd\":\"hello\"}") == refusal("incomplete"));
    }
    SUBCASE("no randomness") {
        Bench b;
        b.boot();
        b.rig.platform.identity().random_fails = true;
        CHECK(b.say("{\"cmd\":\"hello\"}") == refusal("incomplete"));
    }
    SUBCASE("no running version") {
        Bench b;
        b.rig.platform.dfu().has_running = false;
        b.boot();
        b.hello_with_vectors_nonce();
        CHECK(b.register_with_vectors() == refusal("incomplete"));
    }
}

// Issue #126: the barometer is required, so a unit without one is refused whole.
TEST_CASE("activation: a unit with no barometer neither flies nor answers an activation") {
    Bench b(without(ports::Capability::Baro));
    REQUIRE(b.rig.setup() == Status::Down);
    CHECK(b.say("{\"cmd\":\"hello\"}").empty());
    b.pass(3000);
    CHECK(b.rig.product.radio().arm_count() == 0);
    CHECK_FALSE(b.activation().activated());
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
    ActivationVectors v;

    explicit Desk(std::optional<uint16_t> serial = std::nullopt) : port(serial) {
        link.declare_payload_bytes(kChromePayloadBytes);
        link.raise_link(1);
        dfu.has_running = true;
        dfu.running = ports::ImageVersion{0, 3, 0, 1234};
        unit.load();
        config.set_identity(&identity);
    }

    std::string say(const std::string& json) {
        config.on_rx(frame(json.c_str()));
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
        d.config.set_flight_state(fs);
        d.port.queue_random(d.v.bytes<16>("unit_nonce"));
        d.say("{\"cmd\":\"hello\"}");
        d.say(register_command(d.v.text("server_nonce"), d.v.text("register_signature")));
        REQUIRE(d.config.pending() == comms::Pending::Register);
        d.config.set_flight_state(fs);
        REQUIRE(d.config.pending() == comms::Pending::Register);
        d.config.confirm();
        CHECK(d.link.last().bytes.find(d.v.text("registration_signature")) != std::string::npos);
    }
}

// An activated unit transmits, so it owes every identity command the ground gate the others obey.
TEST_CASE("activation: an activated unit refuses hello, register and unlock off the ground") {
    for (flight::FlightState fs : {flight::FlightState::Unknown, flight::FlightState::Airborne}) {
        CAPTURE(static_cast<int>(fs));
        Desk d(Identity::kSerial);
        REQUIRE(d.identity.activated());
        d.config.set_flight_state(fs);
        CHECK(d.say("{\"cmd\":\"hello\"}") == refusal("in_flight"));
        CHECK(d.say(register_command(d.v.text("server_nonce"), d.v.text("register_signature"))) ==
              refusal("in_flight"));
        CHECK(d.say(unlock_command(298, d.v.text("unlock_signature"))) == refusal("in_flight"));
    }
}

TEST_CASE("activation: an activated unit's register prompt is cancelled at take-off") {
    Desk d(Identity::kSerial);
    d.config.set_flight_state(flight::FlightState::Ground);
    REQUIRE(d.say("{\"cmd\":\"hello\"}").find("\"nonce\"") != std::string::npos);
    d.register_signed_by_the_test_server();
    REQUIRE(d.config.pending() == comms::Pending::Register);
    d.config.set_flight_state(flight::FlightState::Airborne);
    CHECK(d.config.pending() == comms::Pending::None);
    CHECK(d.link.last().bytes == refusal("in_flight"));
}

TEST_CASE("activation: a cell too low to write settings refuses an unlock and keeps the nonce") {
    Desk d;
    d.say("{\"cmd\":\"hello\"}");
    d.config.set_supply_warned(true);
    CHECK(d.unlock_signed_by_the_test_server(0x0042) == refusal("low_power"));
    CHECK(d.port.writes == 0);
    d.config.set_supply_warned(false);
    CHECK(d.unlock_signed_by_the_test_server(0x0042) == "{\"ack\":true,\"reason\":\"unlock\"}");
}

// The server assigns from 1: serial 0 would put 0x000000, read as no address, on the air.
TEST_CASE("activation: an unlock code for serial 0 is refused, however well it is signed") {
    Desk d;
    d.say("{\"cmd\":\"hello\"}");
    CHECK(d.unlock_signed_by_the_test_server(0) == refusal("bad_signature"));
    CHECK(d.port.writes == 0);
    CHECK_FALSE(d.identity.activated());
}

// The nonce has a deadline, not a sweep: the pass that reaches it already finds it dead.
TEST_CASE("activation: the nonce is live until kNonceForgetMs after hello, and dead from then on") {
    constexpr uint32_t kHelloMs = 5000;
    for (uint32_t after :
         {comms::IdentityLink::kNonceForgetMs - 1, comms::IdentityLink::kNonceForgetMs,
          comms::IdentityLink::kNonceForgetMs + 1}) {
        CAPTURE(after);
        Desk d;
        d.config.tick(kHelloMs);
        d.say("{\"cmd\":\"hello\"}");
        d.config.tick(kHelloMs + after);
        const std::string answer = d.unlock_signed_by_the_test_server(0x0042);
        if (after < comms::IdentityLink::kNonceForgetMs)
            CHECK(answer == "{\"ack\":true,\"reason\":\"unlock\"}");
        else
            CHECK(answer == refusal("no_session"));
    }
}

// The widest version dfu formats is the widest frame, and it must fit what register checked.
TEST_CASE("activation: the registration frame at the widest version is exactly the size checked") {
    Desk d;
    d.dfu.running = dfu::kWidestVersion;
    d.say("{\"cmd\":\"hello\"}");
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
}
