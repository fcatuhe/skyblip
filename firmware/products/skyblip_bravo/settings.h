#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SETTINGS_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SETTINGS_H

#include <cstddef>
#include <cstdint>
#include <tuple>

#include "core/events/central.h"
#include "core/util/cbor.h"
#include "core/util/result.h"
#include "products/common/alarm.h"

namespace skyblip::bravo {

using common::Alarm;

constexpr uint32_t kAircraftAddressUnset = 0;
constexpr size_t kPeerAddressBytes = 6;

// INFO: fc 12nov26 #192 pins one Go by its whole Bluetooth address and type, and decides the rest
struct PeerPin {
    uint8_t address[kPeerAddressBytes]{};
    uint8_t address_type{0};
    bool pinned{false};
};

static_assert(kPeerAddressBytes == std::tuple_size<decltype(events::BluetoothAddress::bytes)>(),
              "the pin holds the whole address the link names a central by");

events::BluetoothAddress address_of(const PeerPin& pin);
PeerPin pin_of(const events::BluetoothAddress& address);

struct Settings {
    uint32_t aircraft_address{kAircraftAddressUnset};
    PeerPin peer{};
    Alarm alarm{Alarm::Loud};
    // INFO: fc 13nov26 aircraft_address came from the pinned Go, so that Go clearing it clears it
    // here
    bool aircraft_address_from_go{false};
    // INFO: fc 17nov26 the pinned Go's last alarm, which Bravo plays while paired, link down
    // included
    Alarm go_alarm{Alarm::Loud};
};

constexpr uint8_t kBlobVersion = 4;

constexpr Alarm alarm_in_force(const Settings& settings) {
    return settings.peer.pinned ? settings.go_alarm : settings.alarm;
}

Settings defaults();

Status validate(const Settings& settings);

size_t blob_size();
void to_blob(const Settings& settings, uint8_t* out, size_t capacity);
Status from_blob(const uint8_t* in, size_t length, Settings& out);

void write_settings_fields(cbor::Writer& writer, const Settings& settings);
void write_config_fields(cbor::Writer& writer, const Settings& settings);

Status apply_settings(Settings& settings, const cbor::Reader& frame);

}  // namespace skyblip::bravo

#endif
