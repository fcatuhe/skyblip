// clang-format off
#include <cstdint>

#include "core/comms/peer.h"
#include "core/dfu/update.h"
#include "core/events/input.h"
#include "core/events/link.h"
#include "core/flight/state.h"
#include "core/gnss/acquisition.h"
#include "core/gnss/validity.h"
#include "core/model/aircraft.h"
#include "core/model/band.h"
#include "core/power/charging.h"
#include "core/power/cutoff.h"
#include "core/power/reset_reason.h"
#include "core/radio/log.h"
#include "core/timing/durable_write.h"
#include "core/timing/slot.h"
#include "core/traffic/alarm.h"
#include "ports/adsb_receiver.h"

#pragma GCC diagnostic error "-Wswitch"

namespace skyblip {
namespace {

template <class Enumeration>
constexpr uint32_t code(Enumeration value) {
    return static_cast<uint32_t>(value);
}

constexpr bool on_the_wire(power::ResetReason value) {
    switch (value) {
        case power::ResetReason::Unknown:
        case power::ResetReason::PowerOn:
        case power::ResetReason::Pin:
        case power::ResetReason::Brownout:
        case power::ResetReason::Software:
        case power::ResetReason::Watchdog:
        case power::ResetReason::Lockup:
        case power::ResetReason::ChargerWake:
        case power::ResetReason::LowPowerWake:
        case power::ResetReason::Debug:
            return true;
    }
    return false;
}

constexpr bool on_the_wire(dfu::ImageState value) {
    switch (value) {
        case dfu::ImageState::Confirmed:
        case dfu::ImageState::Probation:
        case dfu::ImageState::Reverted:
        case dfu::ImageState::Refused:
            return true;
    }
    return false;
}

constexpr bool on_the_wire(gnss::FixReject value) {
    switch (value) {
        case gnss::FixReject::None:
        case gnss::FixReject::NoSolution:
        case gnss::FixReject::MissingRmc:
        case gnss::FixReject::MissingGga:
        case gnss::FixReject::Stale:
        case gnss::FixReject::NoDate:
        case gnss::FixReject::Jump:
            return true;
    }
    return false;
}

constexpr bool on_the_wire(gnss::Stage value) {
    switch (value) {
        case gnss::Stage::Silent:
        case gnss::Stage::Blind:
        case gnss::Stage::Solving:
        case gnss::Stage::Fixed:
            return true;
    }
    return false;
}

constexpr bool on_the_wire(radio::Verdict value) {
    switch (value) {
        case radio::Verdict::Transmitted:
        case radio::Verdict::Lost:
        case radio::Verdict::Held:
        case radio::Verdict::Unarmed:
        case radio::Verdict::Received:
        case radio::Verdict::Named:
        case radio::Verdict::BadCrc:
        case radio::Verdict::Unframed:
        case radio::Verdict::Miskeyed:
        case radio::Verdict::Undecoded:
        case radio::Verdict::Unsupported:
        case radio::Verdict::Unattempted:
        case radio::Verdict::Declared:
        case radio::Verdict::Deferred:
        case radio::Verdict::Forced:
        case radio::Verdict::Busy:
            return true;
    }
    return false;
}

constexpr bool on_the_wire(model::Source value) {
    switch (value) {
        case model::Source::AdslDirect:
        case model::Source::AdslUplink:
        case model::Source::Alptas:
        case model::Source::Own:
        case model::Source::Adsb:
            return true;
    }
    return false;
}

constexpr bool on_the_wire(model::Band value) {
    switch (value) {
        case model::Band::M:
        case model::Band::O:
            return true;
    }
    return false;
}

constexpr bool on_the_wire(timing::SlotState value) {
    switch (value) {
        case timing::SlotState::UplinkRxOBand:
        case timing::SlotState::SwitchOBandToMBand:
        case timing::SlotState::Slot0:
        case timing::SlotState::Hop:
        case timing::SlotState::Slot1:
        case timing::SlotState::SwitchMBandToOBand:
            return true;
    }
    return false;
}

constexpr bool on_the_wire(flight::FlightState value) {
    switch (value) {
        case flight::FlightState::Unknown:
        case flight::FlightState::Ground:
        case flight::FlightState::Airborne:
            return true;
    }
    return false;
}

constexpr bool on_the_wire(power::PowerLevel value) {
    switch (value) {
        case power::PowerLevel::Unknown:
        case power::PowerLevel::Normal:
        case power::PowerLevel::Critical:
        case power::PowerLevel::Flat:
        case power::PowerLevel::Low:
            return true;
    }
    return false;
}

constexpr bool on_the_wire(power::ChargeCondition value) {
    switch (value) {
        case power::ChargeCondition::Unknown:
        case power::ChargeCondition::Ok:
        case power::ChargeCondition::TooCold:
        case power::ChargeCondition::TooHot:
            return true;
    }
    return false;
}

constexpr bool on_the_wire(events::Contact value) {
    switch (value) {
        case events::Contact::Button:
        case events::Contact::Pad:
            return true;
    }
    return false;
}

constexpr bool on_the_wire(events::MtuExchangeStarter value) {
    switch (value) {
        case events::MtuExchangeStarter::None:
        case events::MtuExchangeStarter::Central:
        case events::MtuExchangeStarter::Unit:
        case events::MtuExchangeStarter::Both:
            return true;
    }
    return false;
}

constexpr bool on_the_wire(events::Endpoint value) {
    switch (value) {
        case events::Endpoint::Config:
        case events::Endpoint::Nmea:
        case events::Endpoint::Log:
        case events::Endpoint::Peer:
            return true;
    }
    return false;
}

constexpr bool on_the_wire(timing::DurableWriteVerdict value) {
    switch (value) {
        case timing::DurableWriteVerdict::Idle:
        case timing::DurableWriteVerdict::Hold:
        case timing::DurableWriteVerdict::Place:
        case timing::DurableWriteVerdict::Forced:
            return true;
    }
    return false;
}

constexpr bool on_the_wire(power::DurableWrite value) {
    switch (value) {
        case power::DurableWrite::Settings:
        case power::DurableWrite::FlightRecord:
            return true;
    }
    return false;
}

constexpr bool on_the_wire(traffic::Level value) {
    switch (value) {
        case traffic::Level::None:
        case traffic::Level::Advisory:
            return true;
    }
    return false;
}

constexpr bool on_the_wire(ports::ReceiverMode value) {
    switch (value) {
        case ports::ReceiverMode::Sleep:
        case ports::ReceiverMode::StandbyRc:
        case ports::ReceiverMode::StandbyXosc:
        case ports::ReceiverMode::Synthesis:
        case ports::ReceiverMode::Receive:
        case ports::ReceiverMode::Transmit:
        case ports::ReceiverMode::Unknown:
            return true;
    }
    return false;
}

constexpr bool on_the_wire(ports::ReceiverReset value) {
    switch (value) {
        case ports::ReceiverReset::None:
        case ports::ReceiverReset::PowerOn:
        case ports::ReceiverReset::Pin:
        case ports::ReceiverReset::Unknown:
            return true;
    }
    return false;
}

constexpr bool on_the_wire(comms::ExportReference value) {
    switch (value) {
        case comms::ExportReference::None:
        case comms::ExportReference::Go:
        case comms::ExportReference::Own:
            return true;
    }
    return false;
}

static_assert(code(power::ResetReason::Unknown) == 0, "reset UNKNOWN");
static_assert(code(power::ResetReason::PowerOn) == 1, "reset POWER ON");
static_assert(code(power::ResetReason::Pin) == 2, "reset RESET PIN");
static_assert(code(power::ResetReason::Brownout) == 3, "reset BROWNOUT");
static_assert(code(power::ResetReason::Software) == 4, "reset SOFT RESET");
static_assert(code(power::ResetReason::Watchdog) == 5, "reset WATCHDOG");
static_assert(code(power::ResetReason::Lockup) == 6, "reset CPU LOCKUP");
static_assert(code(power::ResetReason::ChargerWake) == 7, "reset CHARGER WAKE");
static_assert(code(power::ResetReason::LowPowerWake) == 8, "reset BUTTON WAKE");
static_assert(code(power::ResetReason::Debug) == 9, "reset DEBUGGER");
static_assert(on_the_wire(power::ResetReason::Unknown), "reset");

static_assert(code(dfu::ImageState::Confirmed) == 0, "image_state confirmed");
static_assert(code(dfu::ImageState::Probation) == 1, "image_state probation");
static_assert(code(dfu::ImageState::Reverted) == 2, "image_state reverted");
static_assert(code(dfu::ImageState::Refused) == 3, "image_state refused");
static_assert(on_the_wire(dfu::ImageState::Confirmed), "image_state");

static_assert(code(gnss::FixReject::None) == 0, "reject NONE");
static_assert(code(gnss::FixReject::NoSolution) == 1, "reject NO SOLUTION");
static_assert(code(gnss::FixReject::MissingRmc) == 2, "reject NO RMC");
static_assert(code(gnss::FixReject::MissingGga) == 3, "reject NO GGA");
static_assert(code(gnss::FixReject::Stale) == 4, "reject STALE");
static_assert(code(gnss::FixReject::NoDate) == 5, "reject NO DATE");
static_assert(code(gnss::FixReject::Jump) == 6, "reject JUMP");
static_assert(on_the_wire(gnss::FixReject::None), "reject");

static_assert(code(gnss::Stage::Silent) == 0, "stage silent");
static_assert(code(gnss::Stage::Blind) == 1, "stage blind");
static_assert(code(gnss::Stage::Solving) == 2, "stage solving");
static_assert(code(gnss::Stage::Fixed) == 3, "stage fixed");
static_assert(on_the_wire(gnss::Stage::Silent), "stage");

static_assert(code(radio::Verdict::Transmitted) == 0, "verdict transmitted");
static_assert(code(radio::Verdict::Lost) == 1, "verdict lost");
static_assert(code(radio::Verdict::Held) == 2, "verdict held");
static_assert(code(radio::Verdict::Unarmed) == 3, "verdict unarmed");
static_assert(code(radio::Verdict::Received) == 4, "verdict received");
static_assert(code(radio::Verdict::Named) == 5, "verdict named");
static_assert(code(radio::Verdict::BadCrc) == 6, "verdict bad_crc");
static_assert(code(radio::Verdict::Unframed) == 7, "verdict unframed");
static_assert(code(radio::Verdict::Miskeyed) == 8, "verdict miskeyed");
static_assert(code(radio::Verdict::Undecoded) == 9, "verdict undecoded");
static_assert(code(radio::Verdict::Unsupported) == 10, "verdict unsupported");
static_assert(code(radio::Verdict::Unattempted) == 11, "verdict unattempted");
static_assert(code(radio::Verdict::Declared) == 12, "verdict declared");
static_assert(code(radio::Verdict::Deferred) == 13, "verdict deferred");
static_assert(code(radio::Verdict::Forced) == 14, "verdict forced");
static_assert(code(radio::Verdict::Busy) == 15, "verdict busy");
static_assert(on_the_wire(radio::Verdict::Transmitted), "verdict");

static_assert(code(model::Source::AdslDirect) == 0, "source adsl_direct");
static_assert(code(model::Source::AdslUplink) == 1, "source adsl_uplink");
static_assert(code(model::Source::Alptas) == 2, "source alptas");
static_assert(code(model::Source::Own) == 3, "source own");
static_assert(code(model::Source::Adsb) == 4, "source adsb");
static_assert(on_the_wire(model::Source::AdslDirect), "source");

static_assert(code(model::Band::M) == 0, "band M");
static_assert(code(model::Band::O) == 1, "band O");
static_assert(on_the_wire(model::Band::M), "band");

static_assert(code(timing::SlotState::UplinkRxOBand) == 0, "slot_state uplink_rx_o_band");
static_assert(code(timing::SlotState::SwitchOBandToMBand) == 1, "slot_state switch_o_band_to_m_band");
static_assert(code(timing::SlotState::Slot0) == 2, "slot_state slot0");
static_assert(code(timing::SlotState::Hop) == 3, "slot_state hop");
static_assert(code(timing::SlotState::Slot1) == 4, "slot_state slot1");
static_assert(code(timing::SlotState::SwitchMBandToOBand) == 5, "slot_state switch_m_band_to_o_band");
static_assert(on_the_wire(timing::SlotState::UplinkRxOBand), "slot_state");

static_assert(code(flight::FlightState::Unknown) == 0, "flight_state unknown");
static_assert(code(flight::FlightState::Ground) == 1, "flight_state on_ground");
static_assert(code(flight::FlightState::Airborne) == 2, "flight_state airborne");
static_assert(on_the_wire(flight::FlightState::Unknown), "flight_state");

static_assert(code(power::PowerLevel::Unknown) == 0, "power_level unknown");
static_assert(code(power::PowerLevel::Normal) == 1, "power_level normal");
static_assert(code(power::PowerLevel::Critical) == 2, "power_level critical");
static_assert(code(power::PowerLevel::Flat) == 3, "power_level flat");
static_assert(code(power::PowerLevel::Low) == 4, "power_level low");
static_assert(on_the_wire(power::PowerLevel::Unknown), "power_level");

static_assert(code(power::ChargeCondition::Unknown) == 0, "charge unknown");
static_assert(code(power::ChargeCondition::Ok) == 1, "charge ok");
static_assert(code(power::ChargeCondition::TooCold) == 2, "charge too_cold");
static_assert(code(power::ChargeCondition::TooHot) == 3, "charge too_hot");
static_assert(on_the_wire(power::ChargeCondition::Unknown), "charge");

static_assert(code(events::Contact::Button) == 0, "contact button");
static_assert(code(events::Contact::Pad) == 1, "contact pad");
static_assert(on_the_wire(events::Contact::Button), "contact");

static_assert(code(events::MtuExchangeStarter::None) == 0, "mtu_exchange_starter none");
static_assert(code(events::MtuExchangeStarter::Central) == 1, "mtu_exchange_starter central");
static_assert(code(events::MtuExchangeStarter::Unit) == 2, "mtu_exchange_starter unit");
static_assert(code(events::MtuExchangeStarter::Both) == 3, "mtu_exchange_starter both");
static_assert(on_the_wire(events::MtuExchangeStarter::None), "mtu_exchange_starter");

static_assert(code(events::Endpoint::Config) == 0, "endpoint config");
static_assert(code(events::Endpoint::Nmea) == 1, "endpoint nmea");
static_assert(code(events::Endpoint::Log) == 2, "endpoint log");
static_assert(code(events::Endpoint::Peer) == 3, "endpoint peer");
static_assert(on_the_wire(events::Endpoint::Config), "endpoint");

static_assert(code(timing::DurableWriteVerdict::Idle) == 0, "placement idle");
static_assert(code(timing::DurableWriteVerdict::Hold) == 1, "placement hold");
static_assert(code(timing::DurableWriteVerdict::Place) == 2, "placement place");
static_assert(code(timing::DurableWriteVerdict::Forced) == 3, "placement forced");
static_assert(on_the_wire(timing::DurableWriteVerdict::Idle), "placement");

static_assert(code(power::DurableWrite::Settings) == 0, "write_kind settings");
static_assert(code(power::DurableWrite::FlightRecord) == 1, "write_kind flight_record");
static_assert(on_the_wire(power::DurableWrite::Settings), "write_kind");

static_assert(code(traffic::Level::None) == 0, "alarm_level none");
static_assert(code(traffic::Level::Advisory) == 1, "alarm_level advisory");
static_assert(on_the_wire(traffic::Level::None), "alarm_level");

static_assert(code(ports::ReceiverMode::Sleep) == 0, "receiver_mode sleep");
static_assert(code(ports::ReceiverMode::StandbyRc) == 1, "receiver_mode standby_rc");
static_assert(code(ports::ReceiverMode::StandbyXosc) == 2, "receiver_mode standby_xosc");
static_assert(code(ports::ReceiverMode::Synthesis) == 3, "receiver_mode synthesis");
static_assert(code(ports::ReceiverMode::Receive) == 4, "receiver_mode receive");
static_assert(code(ports::ReceiverMode::Transmit) == 5, "receiver_mode transmit");
static_assert(code(ports::ReceiverMode::Unknown) == 6, "receiver_mode unknown");
static_assert(on_the_wire(ports::ReceiverMode::Sleep), "receiver_mode");

static_assert(code(ports::ReceiverReset::None) == 0, "receiver_reset none");
static_assert(code(ports::ReceiverReset::PowerOn) == 1, "receiver_reset power_on");
static_assert(code(ports::ReceiverReset::Pin) == 2, "receiver_reset pin");
static_assert(code(ports::ReceiverReset::Unknown) == 3, "receiver_reset unknown");
static_assert(on_the_wire(ports::ReceiverReset::None), "receiver_reset");

static_assert(code(comms::ExportReference::None) == 0, "export_reference none");
static_assert(code(comms::ExportReference::Go) == 1, "export_reference go");
static_assert(code(comms::ExportReference::Own) == 2, "export_reference own");
static_assert(on_the_wire(comms::ExportReference::None), "export_reference");

}
}
