#include "products/skyblip_bravo/settings.h"

#include "core/comms/link_registry.h"
#include "core/settings/address.h"
#include "core/settings/blob.h"

namespace skyblip::bravo {

namespace {

constexpr size_t kPayload = sizeof(Settings);

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
    if (settings.alarm_volume > AlarmVolume::Loud) return Status::OutOfRange;
    if (settings.own_icao > settings::kAddressMask) return Status::OutOfRange;
    return Status::Ok;
}

size_t blob_size() { return settings::blob_bytes(kPayload); }

void to_blob(const Settings& settings, uint8_t* out, size_t capacity) {
    settings::seal(kBlobVersion, &settings, kPayload, out, capacity);
}

Status from_blob(const uint8_t* in, size_t length, Settings& out) {
    if (length < settings::kBlobOverhead) return Status::Crc;
    if (settings::blob_version(in) != kBlobVersion) return Status::Unsupported;
    Settings read{};
    const Status status = settings::open(in, length, kPayload, &read);
    if (status != Status::Ok) return status;
    if (validate(read) != Status::Ok) return Status::Invalid;
    out = read;
    return Status::Ok;
}

void write_settings_fields(cbor::Writer& writer, const Settings& settings) {
    writer.add_boolean(link::config_key::kAlarmEnabled, settings.alarm_enabled);
    writer.add_code(link::config_key::kAlarmVolume, settings.alarm_volume);
    writer.add_unsigned(link::config_key::kOwnIcao, settings.own_icao);
}

// INFO: fc 12nov26 a key Bravo does not store is ignored, as the Go ignores address
Status apply_settings(Settings& settings, const cbor::Reader& frame) {
    Settings next = settings;
    Status status = apply_key(frame.read_boolean(link::set_key::kAlarmEnabled), [&](bool value) {
        next.alarm_enabled = value;
        return Status::Ok;
    });
    if (status == Status::Ok)
        status = apply_key(frame.read_unsigned(link::set_key::kAlarmVolume), [&](uint32_t value) {
            if (value > static_cast<uint32_t>(AlarmVolume::Loud)) return Status::OutOfRange;
            next.alarm_volume = static_cast<AlarmVolume>(value);
            return Status::Ok;
        });
    if (status == Status::Ok)
        status = apply_key(frame.read_unsigned(link::set_key::kOwnIcao), [&](uint32_t value) {
            if (value > settings::kAddressMask) return Status::OutOfRange;
            next.own_icao = value;
            return Status::Ok;
        });
    if (status == Status::Ok) status = validate(next);
    if (status != Status::Ok) return status;
    settings = next;
    return Status::Ok;
}

}  // namespace skyblip::bravo
