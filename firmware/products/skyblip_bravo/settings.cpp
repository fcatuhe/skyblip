#include "products/skyblip_bravo/settings.h"

#include "core/comms/link_registry.h"
#include "core/settings/address.h"
#include "core/settings/blob.h"

namespace skyblip::bravo {

namespace {

constexpr size_t kPayload = sizeof(Settings);

// INFO: fc 15nov26 the volume versions 1 and 2 stored, before alarm replaced it and alarm_enabled
enum class AlarmVolume : uint8_t { Off = 0, Soft = 1, Loud = 2 };

// INFO: fc 13nov26 version 1's layout, read once and migrated: it had no aircraft_address_from_go
struct SettingsV1 {
    uint32_t aircraft_address{kAircraftAddressUnset};
    PeerPin peer{};
    bool alarm_enabled{true};
    AlarmVolume alarm_volume{AlarmVolume::Loud};
};

struct SettingsV2 {
    uint32_t aircraft_address{kAircraftAddressUnset};
    PeerPin peer{};
    bool alarm_enabled{true};
    AlarmVolume alarm_volume{AlarmVolume::Loud};
    bool aircraft_address_from_go{false};
};

struct SettingsV3 {
    uint32_t aircraft_address{kAircraftAddressUnset};
    PeerPin peer{};
    Alarm alarm{Alarm::Loud};
    bool aircraft_address_from_go{false};
};

constexpr uint8_t kBlobVersionV1 = 1;
constexpr uint8_t kBlobVersionV2 = 2;
constexpr uint8_t kBlobVersionV3 = 3;

// INFO: fc 15nov26 both old offs silenced the buzzer and the haptic and kept the lamp, as visual
// does
Alarm alarm_of(bool enabled, AlarmVolume volume) {
    if (!enabled || volume == AlarmVolume::Off) return Alarm::Visual;
    return volume == AlarmVolume::Soft ? Alarm::Soft : Alarm::Loud;
}

Status open_v1(const uint8_t* in, size_t length, Settings& out) {
    SettingsV1 read{};
    const Status status = settings::open(in, length, sizeof(SettingsV1), &read);
    if (status != Status::Ok) return status;
    out = Settings{};
    out.aircraft_address = read.aircraft_address;
    out.peer = read.peer;
    out.alarm = alarm_of(read.alarm_enabled, read.alarm_volume);
    out.go_alarm = out.alarm;
    return Status::Ok;
}

Status open_v2(const uint8_t* in, size_t length, Settings& out) {
    SettingsV2 read{};
    const Status status = settings::open(in, length, sizeof(SettingsV2), &read);
    if (status != Status::Ok) return status;
    out = Settings{};
    out.aircraft_address = read.aircraft_address;
    out.peer = read.peer;
    out.alarm = alarm_of(read.alarm_enabled, read.alarm_volume);
    out.aircraft_address_from_go = read.aircraft_address_from_go;
    out.go_alarm = out.alarm;
    return Status::Ok;
}

// INFO: fc 17nov26 a pinned Bravo played its own alarm before version 4, so its Go's starts there
Status open_v3(const uint8_t* in, size_t length, Settings& out) {
    SettingsV3 read{};
    const Status status = settings::open(in, length, sizeof(SettingsV3), &read);
    if (status != Status::Ok) return status;
    out = Settings{};
    out.aircraft_address = read.aircraft_address;
    out.peer = read.peer;
    out.alarm = read.alarm;
    out.aircraft_address_from_go = read.aircraft_address_from_go;
    out.go_alarm = read.alarm;
    return Status::Ok;
}

template <class Value, class Assign>
Status apply_key(const Result<Value>& read, Assign assign) {
    if (read.status() == Status::NotFound) return Status::Ok;
    if (!read.ok()) return read.status();
    return assign(read.value());
}

}  // namespace

Settings defaults() { return Settings{}; }

events::BluetoothAddress address_of(const PeerPin& pin) {
    events::BluetoothAddress address{};
    address.type = pin.address_type;
    for (size_t i = 0; i < kPeerAddressBytes; i++) address.bytes[i] = pin.address[i];
    return address;
}

PeerPin pin_of(const events::BluetoothAddress& address) {
    PeerPin pin{};
    pin.address_type = address.type;
    for (size_t i = 0; i < kPeerAddressBytes; i++) pin.address[i] = address.bytes[i];
    pin.pinned = true;
    return pin;
}

Status validate(const Settings& settings) {
    if (settings.alarm > Alarm::Loud || settings.go_alarm > Alarm::Loud) return Status::OutOfRange;
    if (settings.aircraft_address > settings::kAddressMask) return Status::OutOfRange;
    return Status::Ok;
}

size_t blob_size() { return settings::blob_bytes(kPayload); }

void to_blob(const Settings& settings, uint8_t* out, size_t capacity) {
    settings::seal(kBlobVersion, &settings, kPayload, out, capacity);
}

Status from_blob(const uint8_t* in, size_t length, Settings& out) {
    if (length < settings::kBlobOverhead) return Status::Crc;
    const uint8_t version = settings::blob_version(in);
    Settings read{};
    Status status = Status::Unsupported;
    if (version == kBlobVersion) status = settings::open(in, length, kPayload, &read);
    if (version == kBlobVersionV3) status = open_v3(in, length, read);
    if (version == kBlobVersionV2) status = open_v2(in, length, read);
    if (version == kBlobVersionV1) status = open_v1(in, length, read);
    if (status != Status::Ok) return status;
    if (validate(read) != Status::Ok) return Status::Invalid;
    out = read;
    return Status::Ok;
}

void write_settings_fields(cbor::Writer& writer, const Settings& settings) {
    writer.add_unsigned(link::config_key::kAircraftAddress, settings.aircraft_address);
    writer.add_code(link::config_key::kAlarm, alarm_in_force(settings));
}

void write_config_fields(cbor::Writer& writer, const Settings& settings) {
    write_settings_fields(writer, settings);
    writer.add_boolean(link::config_key::kAlarmFromGo, settings.peer.pinned);
}

// INFO: fc 12nov26 a key Bravo does not store is ignored, as the Go ignores address | 17nov26
// alarm_from_go too
Status apply_settings(Settings& settings, const cbor::Reader& frame) {
    Settings next = settings;
    Status status = apply_key(frame.read_unsigned(link::set_key::kAlarm), [&](uint32_t value) {
        if (value > static_cast<uint32_t>(Alarm::Loud)) return Status::OutOfRange;
        // INFO: fc 17nov26 Unsupported answers set_on_go: a paired Go sets the alarm for both units
        if (settings.peer.pinned) return Status::Unsupported;
        next.alarm = static_cast<Alarm>(value);
        return Status::Ok;
    });
    if (status == Status::Ok)
        status =
            apply_key(frame.read_unsigned(link::set_key::kAircraftAddress), [&](uint32_t value) {
                if (value > settings::kAddressMask) return Status::OutOfRange;
                next.aircraft_address = value;
                next.aircraft_address_from_go = false;
                return Status::Ok;
            });
    if (status == Status::Ok) status = validate(next);
    if (status != Status::Ok) return status;
    settings = next;
    return Status::Ok;
}

}  // namespace skyblip::bravo
