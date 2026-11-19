#include "products/skyblip_go/settings.h"

#include <cstring>

#include "core/comms/link_registry.h"
#include "core/power/battery.h"
#include "core/protocol/adsl.h"
#include "core/protocol/callsign.h"
#include "core/settings/address.h"
#include "core/settings/blob.h"
#include "core/util/cbor.h"

namespace skyblip::go {

namespace {

constexpr size_t kPayload = sizeof(Settings);

bool callsign_is_typeable(const char* callsign) {
    for (size_t i = 0; i < kCallsignBytes; i++) {
        if (callsign[i] == 0) return true;
        if (!protocol::is_callsign_character(callsign[i])) return false;
    }
    return false;
}

template <class Value, class Assign>
Status apply_key(const Result<Value>& read, Assign assign) {
    if (read.status() == Status::NotFound) return Status::Ok;
    if (!read.ok()) return read.status();
    return assign(read.value());
}

}  // namespace

Settings defaults() { return Settings{}; }

Status validate(const Settings& settings) {
    if (settings.version != Settings::kCurrentVersion) return Status::Unsupported;
    if (settings.aircraft_type > 17) return Status::OutOfRange;
    if (settings.alarm > Alarm::Loud) return Status::OutOfRange;
    if (settings.range_step >= kRangeStepCount) return Status::OutOfRange;
    if (settings.plot != RadarPlot::All && settings.plot != RadarPlot::ToScale)
        return Status::OutOfRange;
    if (settings.battery_offset_mv > power::kCalibrationCeilingMv ||
        settings.battery_offset_mv < -power::kCalibrationCeilingMv)
        return Status::OutOfRange;
    if (settings.frequency_trim_e1_ppm > kFrequencyTrimCeilingE1Ppm ||
        settings.frequency_trim_e1_ppm < -kFrequencyTrimCeilingE1Ppm)
        return Status::OutOfRange;
    if (!callsign_is_typeable(settings.callsign)) return Status::Invalid;
    if (settings.aircraft_address != kAircraftAddressUnset &&
        !settings::icao_assignable(settings.aircraft_address))
        return Status::OutOfRange;
    if (settings.bravo.type > kBluetoothAddressTypeCeiling) return Status::OutOfRange;
    return Status::Ok;
}

size_t blob_size() { return settings::blob_bytes(kPayload); }

void to_blob(const Settings& settings, uint8_t* out, size_t capacity) {
    settings::seal(kBlobVersion, &settings, kPayload, out, capacity);
}

Status from_blob(const uint8_t* in, size_t length, Settings& out) {
    if (length < settings::kBlobOverhead) return Status::Crc;
    if (settings::blob_version(in) != kBlobVersion) return Status::Unsupported;
    const Status status = settings::open(in, length, kPayload, &out);
    if (status != Status::Ok) return status;
    if (validate(out) != Status::Ok) return Status::Invalid;
    return Status::Ok;
}

void write_config_fields(cbor::Writer& writer, const Settings& settings,
                         settings::AirAddress address) {
    writer.add_unsigned(link::config_key::kVersion, settings.version);
    if (address) writer.add_unsigned(link::config_key::kAddress, *address);
    writer.add_unsigned(link::config_key::kAddressMappingTable,
                        settings::kSkyblipAddressMappingTable);
    write_settings_fields(writer, settings);
}

void write_settings_fields(cbor::Writer& writer, const Settings& settings) {
    writer.add_unsigned(link::config_key::kAircraftType, settings.aircraft_type);
    writer.add_code(link::config_key::kUnits, settings.units);
    writer.add_text(link::config_key::kCallsign, settings.callsign);
    writer.add_unsigned(link::config_key::kAircraftAddress, settings.aircraft_address);
    writer.add_code(link::config_key::kAlarm, settings.alarm);
}

Status apply_settings(Settings& settings, const cbor::Reader& frame,
                      std::optional<uint32_t> own_transponder) {
    Settings next = settings;
    Status status =
        apply_key(frame.read_unsigned(link::set_key::kAircraftType), [&](uint32_t value) {
            if (value > UINT8_MAX) return Status::OutOfRange;
            next.aircraft_type = static_cast<uint8_t>(value);
            return Status::Ok;
        });
    if (status == Status::Ok)
        status = apply_key(frame.read_unsigned(link::set_key::kAlarm), [&](uint32_t value) {
            if (value > static_cast<uint32_t>(Alarm::Loud)) return Status::OutOfRange;
            next.alarm = static_cast<Alarm>(value);
            return Status::Ok;
        });
    if (status == Status::Ok)
        status = apply_key(frame.read_unsigned(link::set_key::kUnits), [&](uint32_t value) {
            if (value > static_cast<uint32_t>(Units::Metric)) return Status::OutOfRange;
            next.units = static_cast<Units>(value);
            return Status::Ok;
        });
    if (status == Status::Ok)
        status = apply_key(frame.read_integer(link::set_key::kBatteryOffsetMv), [&](int32_t value) {
            if (value < -power::kCalibrationCeilingMv || value > power::kCalibrationCeilingMv)
                return Status::OutOfRange;
            next.battery_offset_mv = static_cast<int16_t>(value);
            next.battery_offset_manual = true;
            return Status::Ok;
        });
    if (status == Status::Ok)
        status =
            apply_key(frame.read_integer(link::set_key::kFrequencyTrimE1Ppm), [&](int32_t value) {
                if (value < -kFrequencyTrimCeilingE1Ppm || value > kFrequencyTrimCeilingE1Ppm)
                    return Status::OutOfRange;
                next.frequency_trim_e1_ppm = static_cast<int16_t>(value);
                return Status::Ok;
            });
    if (status == Status::Ok)
        status =
            apply_key(frame.read_unsigned(link::set_key::kAircraftAddress), [&](uint32_t value) {
                if (value != kAircraftAddressUnset && !settings::icao_assignable(value))
                    return Status::OutOfRange;
                if (contradicts(value, own_transponder)) return Status::Conflict;
                next.aircraft_address = value;
                return Status::Ok;
            });
    if (status == Status::Ok) {
        char callsign[kCallsignBytes] = {0};
        const Status read = frame.read_text(link::set_key::kCallsign, callsign, sizeof(callsign));
        if (read == Status::Ok)
            std::memcpy(next.callsign, callsign, kCallsignBytes);
        else if (read != Status::NotFound)
            status = read;
    }
    if (status == Status::Ok) status = validate(next);
    if (status != Status::Ok) return status;
    settings = next;
    return Status::Ok;
}

bool contradicts(uint32_t aircraft_address, std::optional<uint32_t> own_transponder) {
    return aircraft_address != kAircraftAddressUnset && own_transponder &&
           aircraft_address != *own_transponder;
}

}  // namespace skyblip::go
