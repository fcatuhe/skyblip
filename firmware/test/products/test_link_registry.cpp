// Every frame the unit sends, decoded and held to schemas/link.v1.json.
#include <cstdint>
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
#include "products/skyblip_go/services/diagnostics.h"
#include "products/skyblip_go/settings_store.h"
#include "simulator/world/link_names.h"
#include "test/support/capture_rig.h"
#include "test/support/config_frame.h"
#include "test/support/product_rig.h"
#include "test/support/rig_moves.h"

using namespace skyblip;
namespace names = simulator::link_names;
using platform::host::Identity;

namespace {

const std::set<std::string> kWrittenOnlyByTheHost = {"battery_offset_mv", "frequency_trim_e1_ppm"};

const names::Frame* frame_coded(uint64_t code) {
    for (const names::Frame& frame : names::kFrames)
        if (frame.code == code) return &frame;
    return nullptr;
}

const names::Key* key_numbered(const names::Frame& frame, uint32_t number) {
    for (int i = 0; i < frame.count; i++)
        if (frame.keys[i].number == number) return &frame.keys[i];
    return nullptr;
}

bool names_code(const names::Enumeration& enumeration, uint64_t code) {
    for (int i = 0; i < enumeration.count; i++)
        if (enumeration.values[i].code == code) return true;
    return false;
}

bool carries_its_type(const names::Key& key, const cbor::Entry& entry) {
    switch (key.type) {
        case names::Type::Unsigned: return entry.type == cbor::Type::Unsigned;
        case names::Type::Integer:
            return entry.type == cbor::Type::Unsigned || entry.type == cbor::Type::Negative;
        case names::Type::Boolean: return entry.type == cbor::Type::Boolean;
        case names::Type::Text: return entry.type == cbor::Type::Text;
        case names::Type::Bytes:
            return entry.type == cbor::Type::Bytes &&
                   (key.bytes == 0 || entry.content.size() == static_cast<size_t>(key.bytes));
        case names::Type::UnsignedArray: return entry.type == cbor::Type::Array;
        case names::Type::Enum:
            return entry.type == cbor::Type::Unsigned &&
                   names_code(*key.enumeration, entry.argument);
    }
    return false;
}

class Witness {
   public:
    void hold(const platform::host::Link& link) {
        for (const platform::host::Link::Frame& frame : link.sent)
            if (frame.endpoint != events::Endpoint::Nmea) hold(frame.endpoint, frame.bytes);
    }

    std::vector<std::string> unheard() const {
        std::vector<std::string> out;
        for (const names::Frame& frame : names::kFrames) {
            if (frame.sender == names::Sender::Host) continue;
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
        std::set<uint32_t> present;
        for (int i = 0; i < reader.count(); i++) {
            cbor::Entry entry;
            REQUIRE(reader.entry(i, entry));
            present.insert(entry.key);
            const names::Key* key = key_numbered(*frame, entry.key);
            CHECK_MESSAGE(key != nullptr, entry.key, " is not a key of ",
                          std::string(frame->command));
            if (key == nullptr) continue;
            CHECK_MESSAGE(carries_its_type(*key, entry), std::string(key->name));
            keys_.insert(key);
        }
        for (int i = 0; i < frame->count; i++) {
            const names::Key& key = frame->keys[i];
            if (key.required) CHECK_MESSAGE(present.count(key.number) == 1, std::string(key.name));
        }
    }

    std::set<uint32_t> commands_;
    std::set<const names::Key*> keys_;
};

struct NoConsole {
    bool ready() const { return false; }
    void line(const char*, int) {}
};

std::string hex_text(ConstByteSpan bytes) {
    std::string out(2 * bytes.size(), '0');
    hex::write(bytes, out.data());
    return out;
}

const identity::Nonce kUnitNonce{7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
const identity::Nonce kServerNonce{9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};

std::string register_signature() {
    const crypto::Ed25519::Signature signature = crypto::Ed25519::sign(
        Identity::kTestServerSeed, identity::register_message(kUnitNonce, kServerNonce).bytes());
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
        arm_from_the_page(rig, now_ms);
        capture = rig.product.capture().session_id();
        feed(rig, now_ms, 30);
        rig.double_press(now_ms);
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
};

void speak_every_reply(GroundedUnitWithLogs& unit) {
    unit.rig.raise_link(1);
    for (const char* command :
         {"get", "defaults", "status", "timing", "flash", "radio", "diagnostics", "update"})
        unit.say(std::string("{\"command\":\"") + command + "\"}");
    unit.say("{\"command\":\"set\",\"units\":\"metric\",\"callsign\":\"D-KXYZ\"}");
    unit.confirm();
    unit.say("{\"command\":\"dfu\",\"version\":\"0.4.0+1\"}");
    unit.cancel();
    unit.say("{\"command\":\"gnss_cold_start\"}");
    unit.confirm();
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
    go::SettingsStore store(settings, kTestActivation);
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
    config.set_image_state(dfu::ImageState::Probation,
                           dfu::UpdateRecord{dfu::kWidestVersion, dfu::kWidestVersion});
    config.set_settings_fallback(settings::Fallback::Prior);
    dfu::HubImageReport held{};
    held.holding = dfu::HubImage::Held;
    config.set_hub_image(held);
    for (const char* command : {"status", "diagnostics", "update"})
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
