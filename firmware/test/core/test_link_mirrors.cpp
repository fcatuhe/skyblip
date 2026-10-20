#include <cstdint>

#include "core/comms/diagnostics.h"
#include "core/dfu/update.h"
#include "core/flight/state.h"
#include "core/gnss/validity.h"
#include "core/power/charging.h"
#include "core/power/cutoff.h"
#include "core/power/reset_reason.h"
#include "core/settings/blob.h"
#include "core/store/sector.h"
#include "ports/gnss.h"
#include "products/skyblip_go/settings.h"

#pragma GCC diagnostic error "-Wswitch"

namespace skyblip {

template <class Enumeration>
constexpr uint32_t code(Enumeration value) {
    return static_cast<uint32_t>(value);
}

namespace {

constexpr bool on_the_wire(go::AlarmVolume value) {
    switch (value) {
        case go::AlarmVolume::Off:
        case go::AlarmVolume::Soft:
        case go::AlarmVolume::Loud: return true;
    }
    return false;
}

constexpr bool on_the_wire(go::Units value) {
    switch (value) {
        case go::Units::Nautical:
        case go::Units::Metric: return true;
    }
    return false;
}

constexpr bool on_the_wire(flight::FlightState value) {
    switch (value) {
        case flight::FlightState::Unknown:
        case flight::FlightState::Ground:
        case flight::FlightState::Airborne: return true;
    }
    return false;
}

constexpr bool on_the_wire(power::PowerLevel value) {
    switch (value) {
        case power::PowerLevel::Unknown:
        case power::PowerLevel::Normal:
        case power::PowerLevel::Critical:
        case power::PowerLevel::Flat:
        case power::PowerLevel::Low: return true;
    }
    return false;
}

constexpr bool on_the_wire(comms::DiagnosticsReport::Group value) {
    switch (value) {
        case comms::DiagnosticsReport::Group::System:
        case comms::DiagnosticsReport::Group::Radio:
        case comms::DiagnosticsReport::Group::Traffic:
        case comms::DiagnosticsReport::Group::Gnss:
        case comms::DiagnosticsReport::Group::Power: return true;
    }
    return false;
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
        case power::ResetReason::Debug: return true;
    }
    return false;
}

constexpr bool on_the_wire(ports::GnssConfigStage value) {
    switch (value) {
        case ports::GnssConfigStage::Idle:
        case ports::GnssConfigStage::Restarting:
        case ports::GnssConfigStage::Waking:
        case ports::GnssConfigStage::Identifying:
        case ports::GnssConfigStage::Sending:
        case ports::GnssConfigStage::Verifying:
        case ports::GnssConfigStage::Confirming:
        case ports::GnssConfigStage::Ready:
        case ports::GnssConfigStage::Degraded: return true;
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
        case gnss::FixReject::Jump: return true;
    }
    return false;
}

constexpr bool on_the_wire(power::ChargeCondition value) {
    switch (value) {
        case power::ChargeCondition::Unknown:
        case power::ChargeCondition::Ok:
        case power::ChargeCondition::TooCold:
        case power::ChargeCondition::TooHot: return true;
    }
    return false;
}

constexpr bool on_the_wire(dfu::ImageState value) {
    switch (value) {
        case dfu::ImageState::Confirmed:
        case dfu::ImageState::Probation:
        case dfu::ImageState::Reverted:
        case dfu::ImageState::Refused: return true;
    }
    return false;
}

constexpr bool on_the_wire(settings::Fallback value) {
    switch (value) {
        case settings::Fallback::None:
        case settings::Fallback::Prior:
        case settings::Fallback::Defaults: return true;
    }
    return false;
}

constexpr bool on_the_wire(dfu::HubImage value) {
    switch (value) {
        case dfu::HubImage::None:
        case dfu::HubImage::Missing:
        case dfu::HubImage::Corrupt:
        case dfu::HubImage::Unreadable:
        case dfu::HubImage::Writing:
        case dfu::HubImage::Held: return true;
    }
    return false;
}

constexpr bool on_the_wire(store::SectorOwner value) {
    switch (value) {
        case store::SectorOwner::Flights:
        case store::SectorOwner::Diagnostics: return true;
        case store::SectorOwner::None: return false;
    }
    return false;
}

}

static_assert(code(go::AlarmVolume::Off) == 0, "alarm_volume off");
static_assert(code(go::AlarmVolume::Soft) == 1, "alarm_volume soft");
static_assert(code(go::AlarmVolume::Loud) == 2, "alarm_volume loud");
static_assert(on_the_wire(go::AlarmVolume::Off), "alarm_volume");

static_assert(code(go::Units::Nautical) == 0, "units nautical");
static_assert(code(go::Units::Metric) == 1, "units metric");
static_assert(on_the_wire(go::Units::Nautical), "units");

static_assert(code(flight::FlightState::Unknown) == 0, "flight unknown");
static_assert(code(flight::FlightState::Ground) == 1, "flight ground");
static_assert(code(flight::FlightState::Airborne) == 2, "flight airborne");
static_assert(on_the_wire(flight::FlightState::Unknown), "flight");

static_assert(code(power::PowerLevel::Unknown) == 0, "power_level unknown");
static_assert(code(power::PowerLevel::Normal) == 1, "power_level normal");
static_assert(code(power::PowerLevel::Critical) == 2, "power_level critical");
static_assert(code(power::PowerLevel::Flat) == 3, "power_level flat");
static_assert(code(power::PowerLevel::Low) == 4, "power_level low");
static_assert(on_the_wire(power::PowerLevel::Unknown), "power_level");

static_assert(code(comms::DiagnosticsReport::Group::System) == 0, "group system");
static_assert(code(comms::DiagnosticsReport::Group::Radio) == 1, "group radio");
static_assert(code(comms::DiagnosticsReport::Group::Traffic) == 2, "group traffic");
static_assert(code(comms::DiagnosticsReport::Group::Gnss) == 3, "group gnss");
static_assert(code(comms::DiagnosticsReport::Group::Power) == 4, "group power");
static_assert(on_the_wire(comms::DiagnosticsReport::Group::System), "group");

static_assert(code(power::ResetReason::Unknown) == 0, "reset unknown");
static_assert(code(power::ResetReason::PowerOn) == 1, "reset power_on");
static_assert(code(power::ResetReason::Pin) == 2, "reset pin");
static_assert(code(power::ResetReason::Brownout) == 3, "reset brownout");
static_assert(code(power::ResetReason::Software) == 4, "reset software");
static_assert(code(power::ResetReason::Watchdog) == 5, "reset watchdog");
static_assert(code(power::ResetReason::Lockup) == 6, "reset lockup");
static_assert(code(power::ResetReason::ChargerWake) == 7, "reset charger_wake");
static_assert(code(power::ResetReason::LowPowerWake) == 8, "reset low_power_wake");
static_assert(code(power::ResetReason::Debug) == 9, "reset debug");
static_assert(on_the_wire(power::ResetReason::Unknown), "reset");

static_assert(code(ports::GnssConfigStage::Idle) == 0, "gnss_config idle");
static_assert(code(ports::GnssConfigStage::Restarting) == 1, "gnss_config restarting");
static_assert(code(ports::GnssConfigStage::Waking) == 2, "gnss_config waking");
static_assert(code(ports::GnssConfigStage::Identifying) == 3, "gnss_config identifying");
static_assert(code(ports::GnssConfigStage::Sending) == 4, "gnss_config sending");
static_assert(code(ports::GnssConfigStage::Verifying) == 5, "gnss_config verifying");
static_assert(code(ports::GnssConfigStage::Confirming) == 6, "gnss_config confirming");
static_assert(code(ports::GnssConfigStage::Ready) == 7, "gnss_config ready");
static_assert(code(ports::GnssConfigStage::Degraded) == 8, "gnss_config degraded");
static_assert(on_the_wire(ports::GnssConfigStage::Idle), "gnss_config");

static_assert(code(gnss::FixReject::None) == 0, "reject none");
static_assert(code(gnss::FixReject::NoSolution) == 1, "reject no_solution");
static_assert(code(gnss::FixReject::MissingRmc) == 2, "reject missing_rmc");
static_assert(code(gnss::FixReject::MissingGga) == 3, "reject missing_gga");
static_assert(code(gnss::FixReject::Stale) == 4, "reject stale");
static_assert(code(gnss::FixReject::NoDate) == 5, "reject no_date");
static_assert(code(gnss::FixReject::Jump) == 6, "reject jump");
static_assert(on_the_wire(gnss::FixReject::None), "reject");

static_assert(code(power::ChargeCondition::Unknown) == 0, "charge unknown");
static_assert(code(power::ChargeCondition::Ok) == 1, "charge ok");
static_assert(code(power::ChargeCondition::TooCold) == 2, "charge too_cold");
static_assert(code(power::ChargeCondition::TooHot) == 3, "charge too_hot");
static_assert(on_the_wire(power::ChargeCondition::Unknown), "charge");

static_assert(code(dfu::ImageState::Confirmed) == 0, "image confirmed");
static_assert(code(dfu::ImageState::Probation) == 1, "image probation");
static_assert(code(dfu::ImageState::Reverted) == 2, "image reverted");
static_assert(code(dfu::ImageState::Refused) == 3, "image refused");
static_assert(on_the_wire(dfu::ImageState::Confirmed), "image");

static_assert(code(settings::Fallback::None) == 0, "settings_fallback none");
static_assert(code(settings::Fallback::Prior) == 1, "settings_fallback prior");
static_assert(code(settings::Fallback::Defaults) == 2, "settings_fallback defaults");
static_assert(on_the_wire(settings::Fallback::None), "settings_fallback");

static_assert(code(dfu::HubImage::None) == 0, "hub_image none");
static_assert(code(dfu::HubImage::Missing) == 1, "hub_image missing");
static_assert(code(dfu::HubImage::Corrupt) == 2, "hub_image corrupt");
static_assert(code(dfu::HubImage::Unreadable) == 3, "hub_image unreadable");
static_assert(code(dfu::HubImage::Writing) == 4, "hub_image writing");
static_assert(code(dfu::HubImage::Held) == 5, "hub_image held");
static_assert(on_the_wire(dfu::HubImage::None), "hub_image");

static_assert(code(store::SectorOwner::Flights) == 1, "log flights");
static_assert(code(store::SectorOwner::Diagnostics) == 2, "log diagnostics");
static_assert(on_the_wire(store::SectorOwner::Flights), "log");

}
