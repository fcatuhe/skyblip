// Every frame the unit sends, decoded and held to schemas/link.v1.json, and every command it reads.
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/comms/config.h"
#include "core/comms/link_registry.h"
#include "core/crypto/ed25519.h"
#include "core/identity/messages.h"
#include "core/util/cbor.h"
#include "core/util/hex.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/identity.h"
#include "hardware/platform/host/link.h"
#include "hardware/platform/host/platform.h"
#include "products/skyblip_bravo/radio_protocols.h"
#include "products/skyblip_go/services/diagnostics.h"
#include "products/skyblip_go/settings_store.h"
#include "simulator/world/link_names.h"
#include "test/support/capture_rig.h"
#include "test/support/config_frame.h"
#include "test/support/product_rig.h"
#include "test/support/registry_frames.h"
#include "test/support/rig_moves.h"

using namespace skyblip;
namespace names = simulator::link_names;
using platform::host::Identity;

namespace {

const std::set<std::string> kWrittenOnlyByTheHost = {"battery_offset_mv", "frequency_trim_e1_ppm"};

class Witness {
   public:
    void hold(const platform::host::Link& link) {
        for (const platform::host::Link::Frame& frame : link.sent)
            if (frame.endpoint != events::Endpoint::Nmea) hold(frame.endpoint, frame.bytes);
    }

    std::vector<std::string> unheard() const {
        std::vector<std::string> out;
        for (const names::Frame& frame : names::kFrames) {
            if (frame.sender == names::Sender::Host || !on_a_phone_endpoint(frame)) continue;
            if (commands_.count(frame.code) == 0) out.push_back(frame.command);
            for (int i = 0; i < frame.count; i++) {
                const names::Key& key = frame.keys[i];
                if (key.number != link::kCommandKey && keys_.count(&key) == 0 &&
                    kWrittenOnlyByTheHost.count(key.name) == 0)
                    out.push_back(std::string(frame.command) + "." + key.name);
            }
        }
        return out;
    }

   private:
    void hold(events::Endpoint endpoint, const std::string& bytes) {
        const std::string text = link_text(bytes);
        CAPTURE(text);
        const cbor::Reader reader(
            ConstByteSpan(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()));
        REQUIRE(reader.valid());
        const Result<uint32_t> code = reader.read_unsigned(link::kCommandKey);
        REQUIRE(code.ok());
        const names::Frame* frame = frame_coded(code.value());
        REQUIRE(frame != nullptr);
        CHECK(frame->sender != names::Sender::Host);
        CHECK(frame->endpoint == endpoint);
        commands_.insert(frame->code);
        for (int i = 0; i < reader.count(); i++) {
            cbor::Entry entry;
            REQUIRE(reader.entry(i, entry));
            const names::Key* key = key_numbered(*frame, entry.key);
            CHECK_MESSAGE(key != nullptr, entry.key, " is not a key of ",
                          std::string(frame->command));
            if (key == nullptr) continue;
            CHECK_MESSAGE(carries_its_type(*key, entry), std::string(key->name));
            keys_.insert(key);
        }
        CHECK(missing_required_keys(*frame, reader) == "");
    }

    std::set<uint32_t> commands_;
    std::set<const names::Key*> keys_;
};

struct NoConsole {
    bool ready() const { return false; }
    void line(const char*, int) {}
};

const identity::Nonce kUnitNonce{7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
const identity::Nonce kServerNonce{9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};

std::string register_signature() {
    const crypto::Ed25519::Signature signature = crypto::Ed25519::sign(
        Identity::kTestServerSeed, identity::register_message(kUnitNonce, kServerNonce).bytes());
    return hex_text(ConstByteSpan(signature.data(), signature.size()));
}

const crypto::Ed25519::Seed kBrowserSeed{0x42};

std::string browser_key() {
    const crypto::Ed25519::PublicKey key = crypto::Ed25519::public_key_of(kBrowserSeed);
    return hex_text(ConstByteSpan(key.data(), key.size()));
}

std::string browser_signature() {
    std::string message = "skyblip:browser:v1";
    message.append(reinterpret_cast<const char*>(kUnitNonce.data()), kUnitNonce.size());
    const crypto::Ed25519::Signature signature = crypto::Ed25519::sign(
        kBrowserSeed,
        ConstByteSpan(reinterpret_cast<const uint8_t*>(message.data()), message.size()));
    return hex_text(ConstByteSpan(signature.data(), signature.size()));
}

std::string unlock_signature() {
    const identity::UnlockCode code = Identity::unlock_code_for(Identity::kSerial, kUnitNonce);
    return hex_text(ConstByteSpan(code.signature.data(), code.signature.size()));
}

struct GroundedUnitWithLogs {
    Rig rig;
    uint32_t now_ms{100};
    uint32_t flight{0};
    uint32_t capture{0};

    GroundedUnitWithLogs() {
        rig.platform.dfu().running_version_valid = true;
        rig.platform.dfu().running_image_version = ports::ImageVersion{0, 3, 0, 1234};
        REQUIRE(rig.setup() == Status::Ok);
        taxi(rig, now_ms, 5);
        arm_over_the_link(rig, now_ms);
        capture = rig.product.capture().session_id();
        feed(rig, now_ms, 30);
        stop_over_the_link(rig, now_ms);
        taxi(rig, now_ms, 20);
        fly(rig, now_ms, 80);
        taxi(rig, now_ms, 40);
        flight = rig.product.flight_log().session_id();
        REQUIRE_FALSE(rig.product.capture().capturing());
        REQUIRE_FALSE(rig.product.flight_log().recording());
        go::DiagnosticsDump<Go, NoConsole>().step(rig.product, now_ms);
    }

    void say(events::Endpoint endpoint, const std::string& json) {
        rig.platform.link().push_rx(link_frame(endpoint, 1, json.c_str()));
        taxi(rig, now_ms, 1);
    }
    void say(const std::string& json) { say(events::Endpoint::Config, json); }
    void say_on_log(const std::string& json) { say(events::Endpoint::Log, json); }

    void confirm() {
        taxi(rig, now_ms, 3);
        rig.double_press(now_ms);
        taxi(rig, now_ms, 2);
    }

    void cancel() {
        taxi(rig, now_ms, 3);
        rig.press(now_ms);
        taxi(rig, now_ms, 2);
    }

    void reconnect() {
        rig.drop_link(1);
        taxi(rig, now_ms, 1);
        rig.raise_link(1);
        taxi(rig, now_ms, 1);
    }

    void outwait_prompt() { taxi(rig, now_ms, comms::kPromptCeilingMs / 1000 + 1); }

    bool answered(size_t before, events::Endpoint endpoint, const std::string& prefix) {
        const auto& sent = rig.platform.link().sent;
        for (size_t i = before; i < sent.size(); i++)
            if (sent[i].endpoint == endpoint && link_text(sent[i].bytes).rfind(prefix, 0) == 0)
                return true;
        return false;
    }
};

void speak_every_reply(GroundedUnitWithLogs& unit) {
    unit.rig.raise_link(1);
    for (const char* command : {"get", "defaults", "status", "timing", "flash", "radio",
                                "diagnostics", "update", "install_report", "protocols"})
        unit.say(std::string("{\"command\":\"") + command + "\"}");
    unit.say("{\"command\":\"set\",\"units\":\"metric\",\"callsign\":\"D-KXYZ\"}");
    unit.confirm();
    unit.say("{\"command\":\"dfu\",\"version\":\"0.4.0+1\"}");
    unit.cancel();
    unit.reconnect();
    unit.say("{\"command\":\"gnss_cold_start\"}");
    unit.confirm();
    const std::string trust = "{\"command\":\"trust_browser\",\"key\":\"" + browser_key() +
                              "\",\"signature\":\"" + browser_signature() +
                              "\",\"label\":\"Chrome on Android\"}";
    unit.rig.platform.identity().queue_random(kUnitNonce);
    unit.say("{\"command\":\"browser_challenge\"}");
    unit.say(trust);
    unit.say("{\"command\":\"set\",\"units\":\"nautical\"}");
    unit.confirm();
    unit.reconnect();
    unit.rig.platform.identity().queue_random(kUnitNonce);
    unit.say("{\"command\":\"browser_challenge\"}");
    unit.say(trust);
    unit.say("{\"command\":\"browsers\",\"index\":0}");
    unit.say("{\"command\":\"browsers\",\"index\":1}");
    unit.rig.platform.identity().queue_random(kUnitNonce);
    unit.say("{\"command\":\"challenge\"}");
    unit.say("{\"command\":\"register\",\"server_nonce\":\"" +
             hex_text(ConstByteSpan(kServerNonce.data(), kServerNonce.size())) +
             "\",\"signature\":\"" + register_signature() + "\"}");
    unit.confirm();
    unit.say("{\"command\":\"unlock\",\"serial\":" + std::to_string(Identity::kSerial) +
             ",\"signature\":\"" + unlock_signature() + "\"}");
    taxi(unit.rig, unit.now_ms, 3);
    unit.rig.raise_link(2);
    unit.rig.send_from(2, "{\"command\":\"get\"}");
    taxi(unit.rig, unit.now_ms, 1);

    unit.say_on_log("{\"command\":\"list\"}");
    unit.say_on_log("{\"command\":\"list\",\"index\":0}");
    unit.say_on_log("{\"command\":\"read\",\"session\":" + std::to_string(unit.flight) +
                    ",\"count\":2}");
    unit.say_on_log("{\"command\":\"list\",\"log\":\"diagnostics\",\"index\":0}");
    unit.say_on_log("{\"command\":\"read\",\"log\":\"diagnostics\",\"session\":" +
                    std::to_string(unit.capture) + "}");
    unit.say_on_log("{\"command\":\"erase\",\"log\":\"diagnostics\"}");
    unit.say_on_log("{\"command\":\"erase\"}");
    unit.confirm();
    taxi(unit.rig, unit.now_ms, 30);
}

power::BatteryState cell_off_the_cable() {
    power::BatteryState battery{};
    battery.cell_mv = 3900;
    battery.percent = 64;
    battery.valid = true;
    return battery;
}

void speak_every_key_the_rig_leaves_out(platform::host::Link& link) {
    platform::host::Watchdog watchdog;
    platform::host::Dfu dfu{watchdog};
    dfu.running_key_valid = true;
    dfu.allows_downgrade = true;
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation, kNoOwnTransponder);
    comms::ConfigService config(link, store, &dfu);
    link.raise_link(1);
    link.declare_payload_bytes(495);
    config.set_battery_state(cell_off_the_cable(), power::PowerLevel::Normal);
    config.set_die_temperature(-415, true);
    comms::Diagnostics& dump = config.diagnostics();
    dump.refreshes = 1;
    dump.gnss_solution_phase_valid = true;
    dump.residual_valid = true;
    dump.charge = power::ChargeCondition::TooCold;
    dump.adsb_fitted = true;
    dump.capture_available = true;
    dump.capture_armed = true;
    config.set_image_state(dfu::ImageState::Probation,
                           dfu::UpdateRecord{dfu::kWidestVersion, dfu::kWidestVersion});
    config.set_settings_fallback(settings::Fallback::Prior);
    dfu::HubImageReport held{};
    held.holding = dfu::HubImage::Held;
    config.set_hub_image(held);
    config.set_product(dfu::Product::SkyblipBravo);
    config.set_bravo_state(comms::BravoState::NoUtc);
    dfu::InstallReport report{};
    report.request = {ports::StagedCheck::Matched, -5, ports::SwapType::Test};
    report.booted(/*on_probation=*/true, power::ResetReason::Watchdog);
    config.set_install_report(&report);
    dfu.staged_key_valid = true;
    config.on_rx(config_frame("{\"command\":\"dfu\",\"version\":\"0.2.0+15\"}"));
    config.confirm();
    dfu.finished_upload = true;
    config.tick(0);
    config.set_radio_protocols(bravo::kRadioProtocols);
    for (const char* command : {"status", "diagnostics", "update", "install_report", "protocols"})
        config.on_rx(config_frame((std::string("{\"command\":\"") + command + "\"}").c_str()));
}

}  // namespace

// The registry's keys and flags were written from the code: this holds them to what it writes.
TEST_CASE("link registry: every frame the unit sends carries its keys, types and codes, no other") {
    Witness witness;
    GroundedUnitWithLogs unit;
    speak_every_reply(unit);
    witness.hold(unit.rig.platform.link());
    platform::host::Link desk;
    speak_every_key_the_rig_leaves_out(desk);
    witness.hold(desk);
    CHECK(witness.unheard() == std::vector<std::string>{});
}

namespace {

struct Answer {
    const char* command;
    events::Endpoint endpoint;
    std::string reads;
    const char* foreign_key_refused_as;
};

constexpr events::Endpoint kConfig = events::Endpoint::Config;
constexpr events::Endpoint kLog = events::Endpoint::Log;

std::string reply(const char* command) { return std::string("{\"command\":\"") + command + "\""; }

std::string prompt(const char* reason) {
    return std::string(
               "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"pending\":true,"
               "\"reason\":\"") +
           reason + "\"}";
}

const std::vector<Answer> kAnswers = {
    {"get", kConfig, reply("config"), "invalid"},
    {"defaults", kConfig, reply("defaults"), "invalid"},
    {"set", kConfig, prompt("confirm"), "invalid"},
    {"status", kConfig, reply("status"), "invalid"},
    {"timing", kConfig, reply("timing"), "invalid"},
    {"flash", kConfig, reply("flash"), "invalid"},
    {"radio", kConfig, reply("radio"), "invalid"},
    {"diagnostics", kConfig, reply("diagnostics"), "invalid"},
    {"update", kConfig, reply("update"), "invalid"},
    {"install_report", kConfig, reply("install_report"), "invalid"},
    {"protocols", kConfig, reply("protocols"), "invalid"},
    {"dfu", kConfig, prompt("confirm_dfu"), "invalid"},
    {"recovery", kConfig, prompt("confirm_recovery"), "invalid"},
    {"power_off", kConfig, prompt("confirm_power_off"), "invalid"},
    {"challenge", kConfig, reply("challenge"), "invalid"},
    {"register", kConfig, prompt("confirm_register"), "invalid"},
    {"unlock", kConfig,
     "{\"command\":\"acknowledgement\",\"acknowledge\":true,\"reason\":\"unlock\"}", "invalid"},
    {"list", kLog, reply("session"), "invalid"},
    {"read", kLog, reply("chunk"), "invalid"},
    {"erase", kConfig, prompt("confirm_erase_log"), "invalid"},
    {"gnss_cold_start", kConfig, prompt("confirm_gnss_cold_start"), "invalid"},
    {"ownship", kConfig,
     "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"unknown_command\"}",
     "unknown_command"},
    {"capture", kConfig, prompt("confirm_capture"), "invalid"},
    {"browser_challenge", kConfig, reply("browser_challenge"), "invalid"},
    {"trust_browser", kConfig,
     "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"untrusted\"}", "invalid"},
    {"browsers", kConfig,
     "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"untrusted\"}", "invalid"},
    {"forget_browser", kConfig,
     "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"untrusted\"}", "invalid"},
};

const Answer* answer_to(const names::Frame& frame) {
    for (const Answer& answer : kAnswers)
        if (std::string(answer.command) == frame.command) return &answer;
    return nullptr;
}

std::map<std::string, std::string> samples(const GroundedUnitWithLogs& unit) {
    return {
        {"set.version", "1"},
        {"set.address", "298"},
        {"set.address_mapping_table", "58"},
        {"set.aircraft_type", "1"},
        {"set.alarm_enabled", "true"},
        {"set.alarm_volume", "\"soft\""},
        {"set.units", "\"metric\""},
        {"set.callsign", "\"D-KXYZ\""},
        {"set.battery_offset_mv", "10"},
        {"set.frequency_trim_e1_ppm", "-5"},
        {"set.own_icao", "3958150"},
        {"dfu.version", "\"0.4.0+1\""},
        {"register.server_nonce",
         "\"" + hex_text(ConstByteSpan(kServerNonce.data(), kServerNonce.size())) + "\""},
        {"register.signature", "\"" + register_signature() + "\""},
        {"unlock.serial", std::to_string(Identity::kSerial)},
        {"unlock.signature", "\"" + unlock_signature() + "\""},
        {"list.log", "\"flights\""},
        {"list.index", "0"},
        {"read.log", "\"flights\""},
        {"read.session", std::to_string(unit.flight)},
        {"read.from", "0"},
        {"read.count", "1"},
        {"erase.log", "\"flights\""},
        {"ownship.aircraft_type", "1"},
        {"ownship.width", "8"},
        {"ownship.hot_spot", "2"},
        {"ownship.rows", "\"00ff00ff\""},
        {"capture.profile", "\"full\""},
        {"trust_browser.key", "\"" + browser_key() + "\""},
        {"trust_browser.signature", "\"" + browser_signature() + "\""},
        {"trust_browser.label", "\"Chrome on Android\""},
        {"browsers.index", "0"},
        {"forget_browser.id", "\"" + browser_key().substr(0, 16) + "\""},
    };
}

std::string with_every_key(const names::Frame& frame,
                           const std::map<std::string, std::string>& values) {
    std::string json = std::string("{\"command\":\"") + frame.command + "\"";
    if (frame.sender == names::Sender::Both) return json + "}";
    for (int i = 0; i < frame.count; i++) {
        const names::Key& key = frame.keys[i];
        if (key.number == link::kCommandKey) continue;
        const std::string name = std::string(frame.command) + "." + key.name;
        REQUIRE_MESSAGE(values.count(name) == 1, "no sample for ", name);
        json += ",\"" + std::string(key.name) + "\":" + values.at(name);
    }
    return json + "}";
}

events::RxFrame with_a_foreign_key(const names::Frame& frame) {
    uint32_t foreign = 1;
    if (frame.sender == names::Sender::Host)
        for (int i = 0; i < frame.count; i++) foreign = std::max(foreign, frame.keys[i].number + 1);
    events::RxFrame out{};
    out.session_id = 1;
    out.endpoint = frame.endpoint;
    cbor::Writer writer(out.data.data(), static_cast<int>(out.data.size()));
    writer.add_unsigned(link::kCommandKey, frame.code);
    writer.add_unsigned(foreign, 0);
    out.bytes = static_cast<uint16_t>(writer.finish());
    return out;
}

std::string refusal(const names::Frame& frame, const Answer& answer) {
    const std::string command = frame.endpoint == kLog ? "log" : "acknowledgement";
    return "{\"command\":\"" + command + "\",\"acknowledge\":false,\"reason\":\"" +
           answer.foreign_key_refused_as + "\"}";
}

}  // namespace

// A host frame the firmware ignored a key of, or refused one it lists, was invisible to a page.
TEST_CASE("link registry: the unit reads every key a host frame may carry, and refuses any other") {
    GroundedUnitWithLogs unit;
    unit.rig.raise_link(1);
    unit.rig.platform.identity().queue_random(kUnitNonce);
    unit.rig.platform.identity().queue_random(kUnitNonce);
    const std::map<std::string, std::string> values = samples(unit);
    for (const names::Frame& frame : names::kFrames) {
        if (frame.sender == names::Sender::Unit || !on_a_phone_endpoint(frame)) continue;
        const Answer* answer = answer_to(frame);
        REQUIRE_MESSAGE(answer != nullptr, "no answer for ", std::string(frame.command));
        const std::string json = with_every_key(frame, values);
        CAPTURE(json);

        size_t before = unit.rig.platform.link().sent.size();
        unit.say(frame.endpoint, json);
        taxi(unit.rig, unit.now_ms, 3);
        CHECK(unit.answered(before, answer->endpoint, answer->reads));
        if (answer->reads.find("\"pending\":true") != std::string::npos) unit.outwait_prompt();

        before = unit.rig.platform.link().sent.size();
        unit.rig.platform.link().push_rx(with_a_foreign_key(frame));
        taxi(unit.rig, unit.now_ms, 3);
        CHECK(unit.answered(before, frame.endpoint, refusal(frame, *answer)));
    }
}
