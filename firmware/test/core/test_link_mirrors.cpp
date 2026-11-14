#include <cstdint>

#include "core/comms/diagnostics.h"
#include "core/comms/peer.h"
#include "core/dfu/update.h"
#include "core/flight/state.h"
#include "core/gnss/validity.h"
#include "core/model/aircraft.h"
#include "core/power/charging.h"
#include "core/power/cutoff.h"
#include "core/power/reset_reason.h"
#include "core/settings/blob.h"
#include "core/store/sector.h"
#include "ports/dfu.h"
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
        case comms::DiagnosticsReport::Group::Power:
        case comms::DiagnosticsReport::Group::Adsb:
        case comms::DiagnosticsReport::Group::Capture: return true;
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

constexpr bool on_the_wire(comms::ExportReference value) {
    switch (value) {
        case comms::ExportReference::None:
        case comms::ExportReference::Go:
        case comms::ExportReference::Own: return true;
    }
    return false;
}

constexpr bool on_the_wire(comms::BravoState value) {
    switch (value) {
        case comms::BravoState::Waiting:
        case comms::BravoState::Live:
        case comms::BravoState::LinkDown:
        case comms::BravoState::ReceiverDown:
        case comms::BravoState::NoUtc: return true;
        case comms::BravoState::Unpaired: return false;
    }
    return false;
}

constexpr bool on_the_wire(model::ClimbSource value) {
    switch (value) {
        case model::ClimbSource::Unknown:
        case model::ClimbSource::Gnss:
        case model::ClimbSource::Barometric: return true;
    }
    return false;
}

constexpr bool on_the_wire(dfu::Product value) {
    switch (value) {
        case dfu::Product::SkyblipGo:
        case dfu::Product::SkyblipBravo: return true;
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

constexpr bool on_the_wire(comms::CaptureProfile value) {
    switch (value) {
        case comms::CaptureProfile::Full:
        case comms::CaptureProfile::PowerRun:
        case comms::CaptureProfile::FlightRun:
        case comms::CaptureProfile::Stop: return true;
    }
    return false;
}

constexpr bool on_the_wire(ports::StagedCheck value) {
    switch (value) {
        case ports::StagedCheck::Unchecked:
        case ports::StagedCheck::Matched:
        case ports::StagedCheck::Damaged:
        case ports::StagedCheck::Unreadable:
        case ports::StagedCheck::Undigested: return true;
    }
    return false;
}

constexpr bool on_the_wire(ports::SwapType value) {
    switch (value) {
        case ports::SwapType::Unknown:
        case ports::SwapType::None:
        case ports::SwapType::Test:
        case ports::SwapType::Permanent:
        case ports::SwapType::Revert:
        case ports::SwapType::Fail:
        case ports::SwapType::Panic: return true;
    }
    return false;
}

constexpr bool on_the_wire(dfu::Proof value) {
    switch (value) {
        case dfu::Proof::None:
        case dfu::Proof::Booted:
        case dfu::Proof::Started:
        case dfu::Proof::GnssHeard:
        case dfu::Proof::HubBootable:
        case dfu::Proof::Proven:
        case dfu::Proof::ConfirmFailed: return true;
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
static_assert(code(comms::DiagnosticsReport::Group::Adsb) == 5, "group adsb");
static_assert(code(comms::DiagnosticsReport::Group::Capture) == 6, "group capture");
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

static_assert(code(comms::ExportReference::None) == 0, "export_reference none");
static_assert(code(comms::ExportReference::Go) == 1, "export_reference go");
static_assert(code(comms::ExportReference::Own) == 2, "export_reference own");
static_assert(on_the_wire(comms::ExportReference::None), "export_reference");

static_assert(code(comms::BravoState::Waiting) == 1, "bravo waiting");
static_assert(code(comms::BravoState::Live) == 2, "bravo live");
static_assert(code(comms::BravoState::LinkDown) == 3, "bravo link_down");
static_assert(code(comms::BravoState::ReceiverDown) == 4, "bravo receiver_down");
static_assert(code(comms::BravoState::NoUtc) == 5, "bravo no_utc");
static_assert(on_the_wire(comms::BravoState::Waiting), "bravo");

static_assert(code(model::ClimbSource::Unknown) == 0, "climb_source unknown");
static_assert(code(model::ClimbSource::Gnss) == 1, "climb_source gnss");
static_assert(code(model::ClimbSource::Barometric) == 2, "climb_source barometric");
static_assert(on_the_wire(model::ClimbSource::Unknown), "climb_source");

static_assert(code(dfu::Product::SkyblipGo) == 0, "product skyblip_go");
static_assert(code(dfu::Product::SkyblipBravo) == 1, "product skyblip_bravo");
static_assert(on_the_wire(dfu::Product::SkyblipGo), "product");

static_assert(code(store::SectorOwner::Flights) == 1, "log flights");
static_assert(code(store::SectorOwner::Diagnostics) == 2, "log diagnostics");
static_assert(on_the_wire(store::SectorOwner::Flights), "log");

static_assert(code(comms::CaptureProfile::Full) == 0, "capture_profile full");
static_assert(code(comms::CaptureProfile::PowerRun) == 1, "capture_profile power_run");
static_assert(code(comms::CaptureProfile::FlightRun) == 2, "capture_profile flight_run");
static_assert(code(comms::CaptureProfile::Stop) == 3, "capture_profile stop");
static_assert(on_the_wire(comms::CaptureProfile::Full), "capture_profile");

static_assert(code(ports::StagedCheck::Unchecked) == 0, "staged_check unchecked");
static_assert(code(ports::StagedCheck::Matched) == 1, "staged_check matched");
static_assert(code(ports::StagedCheck::Damaged) == 2, "staged_check damaged");
static_assert(code(ports::StagedCheck::Unreadable) == 3, "staged_check unreadable");
static_assert(code(ports::StagedCheck::Undigested) == 4, "staged_check undigested");
static_assert(on_the_wire(ports::StagedCheck::Unchecked), "staged_check");

static_assert(code(ports::SwapType::Unknown) == 0, "swap_type unknown");
static_assert(code(ports::SwapType::None) == 1, "swap_type none");
static_assert(code(ports::SwapType::Test) == 2, "swap_type test");
static_assert(code(ports::SwapType::Permanent) == 3, "swap_type permanent");
static_assert(code(ports::SwapType::Revert) == 4, "swap_type revert");
static_assert(code(ports::SwapType::Fail) == 5, "swap_type fail");
static_assert(code(ports::SwapType::Panic) == 6, "swap_type panic");
static_assert(on_the_wire(ports::SwapType::Unknown), "swap_type");

static_assert(code(dfu::Proof::None) == 0, "proof none");
static_assert(code(dfu::Proof::Booted) == 1, "proof booted");
static_assert(code(dfu::Proof::Started) == 2, "proof started");
static_assert(code(dfu::Proof::GnssHeard) == 3, "proof gnss_heard");
static_assert(code(dfu::Proof::HubBootable) == 4, "proof hub_bootable");
static_assert(code(dfu::Proof::Proven) == 5, "proof proven");
static_assert(code(dfu::Proof::ConfirmFailed) == 6, "proof confirm_failed");
static_assert(on_the_wire(dfu::Proof::None), "proof");

}
