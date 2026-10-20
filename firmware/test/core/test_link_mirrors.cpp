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

namespace skyblip {

template <class Enumeration>
constexpr uint32_t code(Enumeration value) {
    return static_cast<uint32_t>(value);
}

static_assert(code(go::AlarmVolume::Off) == 0, "alarm_volume off");
static_assert(code(go::AlarmVolume::Soft) == 1, "alarm_volume soft");
static_assert(code(go::AlarmVolume::Loud) == 2, "alarm_volume loud");

static_assert(code(go::Units::Nautical) == 0, "units nautical");
static_assert(code(go::Units::Metric) == 1, "units metric");

static_assert(code(flight::FlightState::Unknown) == 0, "flight unknown");
static_assert(code(flight::FlightState::Ground) == 1, "flight ground");
static_assert(code(flight::FlightState::Airborne) == 2, "flight airborne");

static_assert(code(power::PowerLevel::Unknown) == 0, "power_level unknown");
static_assert(code(power::PowerLevel::Normal) == 1, "power_level normal");
static_assert(code(power::PowerLevel::Critical) == 2, "power_level critical");
static_assert(code(power::PowerLevel::Flat) == 3, "power_level flat");
static_assert(code(power::PowerLevel::Low) == 4, "power_level low");

static_assert(code(comms::DiagnosticsReport::Group::System) == 0, "group system");
static_assert(code(comms::DiagnosticsReport::Group::Radio) == 1, "group radio");
static_assert(code(comms::DiagnosticsReport::Group::Traffic) == 2, "group traffic");
static_assert(code(comms::DiagnosticsReport::Group::Gnss) == 3, "group gnss");
static_assert(code(comms::DiagnosticsReport::Group::Power) == 4, "group power");

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

static_assert(code(ports::GnssConfigStage::Idle) == 0, "gnss_config idle");
static_assert(code(ports::GnssConfigStage::Restarting) == 1, "gnss_config restarting");
static_assert(code(ports::GnssConfigStage::Waking) == 2, "gnss_config waking");
static_assert(code(ports::GnssConfigStage::Identifying) == 3, "gnss_config identifying");
static_assert(code(ports::GnssConfigStage::Sending) == 4, "gnss_config sending");
static_assert(code(ports::GnssConfigStage::Verifying) == 5, "gnss_config verifying");
static_assert(code(ports::GnssConfigStage::Confirming) == 6, "gnss_config confirming");
static_assert(code(ports::GnssConfigStage::Ready) == 7, "gnss_config ready");
static_assert(code(ports::GnssConfigStage::Degraded) == 8, "gnss_config degraded");

static_assert(code(gnss::FixReject::None) == 0, "reject none");
static_assert(code(gnss::FixReject::NoSolution) == 1, "reject no_solution");
static_assert(code(gnss::FixReject::MissingRmc) == 2, "reject missing_rmc");
static_assert(code(gnss::FixReject::MissingGga) == 3, "reject missing_gga");
static_assert(code(gnss::FixReject::Stale) == 4, "reject stale");
static_assert(code(gnss::FixReject::NoDate) == 5, "reject no_date");
static_assert(code(gnss::FixReject::Jump) == 6, "reject jump");

static_assert(code(power::ChargeCondition::Unknown) == 0, "charge unknown");
static_assert(code(power::ChargeCondition::Ok) == 1, "charge ok");
static_assert(code(power::ChargeCondition::TooCold) == 2, "charge too_cold");
static_assert(code(power::ChargeCondition::TooHot) == 3, "charge too_hot");

static_assert(code(dfu::ImageState::Confirmed) == 0, "image confirmed");
static_assert(code(dfu::ImageState::Probation) == 1, "image probation");
static_assert(code(dfu::ImageState::Reverted) == 2, "image reverted");
static_assert(code(dfu::ImageState::Refused) == 3, "image refused");

static_assert(code(settings::Fallback::None) == 0, "settings_fallback none");
static_assert(code(settings::Fallback::Prior) == 1, "settings_fallback prior");
static_assert(code(settings::Fallback::Defaults) == 2, "settings_fallback defaults");

static_assert(code(dfu::HubImage::None) == 0, "hub_image none");
static_assert(code(dfu::HubImage::Missing) == 1, "hub_image missing");
static_assert(code(dfu::HubImage::Corrupt) == 2, "hub_image corrupt");
static_assert(code(dfu::HubImage::Unreadable) == 3, "hub_image unreadable");
static_assert(code(dfu::HubImage::Writing) == 4, "hub_image writing");
static_assert(code(dfu::HubImage::Held) == 5, "hub_image held");

static_assert(code(store::SectorOwner::Flights) == 1, "log flights");
static_assert(code(store::SectorOwner::Diagnostics) == 2, "log diagnostics");

}
