// Every enum a diagnostics payload writes as a byte, pinned to the code the corpus holds.
#include "core/diagnostics/payload.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {

constexpr uint8_t kUnpinned = 0xFF;

// No default in any switch below: a member added to one of these enums fails this build, naming it.
uint8_t wire(power::ResetReason value) {
    switch (value) {
        case power::ResetReason::Unknown: return 0;
        case power::ResetReason::PowerOn: return 1;
        case power::ResetReason::Pin: return 2;
        case power::ResetReason::Brownout: return 3;
        case power::ResetReason::Software: return 4;
        case power::ResetReason::Watchdog: return 5;
        case power::ResetReason::Lockup: return 6;
        case power::ResetReason::ChargerWake: return 7;
        case power::ResetReason::LowPowerWake: return 8;
        case power::ResetReason::Debug: return 9;
    }
    return kUnpinned;
}

uint8_t wire(diagnostics::Profile value) {
    switch (value) {
        case diagnostics::Profile::Full: return 0;
        case diagnostics::Profile::PowerRun: return 1;
        case diagnostics::Profile::FlightRun: return 2;
    }
    return kUnpinned;
}

uint8_t wire(dfu::ImageState value) {
    switch (value) {
        case dfu::ImageState::Confirmed: return 0;
        case dfu::ImageState::Probation: return 1;
        case dfu::ImageState::Reverted: return 2;
        case dfu::ImageState::Refused: return 3;
    }
    return kUnpinned;
}

uint8_t wire(gnss::FixReject value) {
    switch (value) {
        case gnss::FixReject::None: return 0;
        case gnss::FixReject::NoSolution: return 1;
        case gnss::FixReject::MissingRmc: return 2;
        case gnss::FixReject::MissingGga: return 3;
        case gnss::FixReject::Stale: return 4;
        case gnss::FixReject::NoDate: return 5;
        case gnss::FixReject::Jump: return 6;
    }
    return kUnpinned;
}

uint8_t wire(gnss::Stage value) {
    switch (value) {
        case gnss::Stage::Silent: return 0;
        case gnss::Stage::Blind: return 1;
        case gnss::Stage::Solving: return 2;
        case gnss::Stage::Fixed: return 3;
    }
    return kUnpinned;
}

uint8_t wire(radio::Verdict value) {
    switch (value) {
        case radio::Verdict::Transmitted: return 0;
        case radio::Verdict::Lost: return 1;
        case radio::Verdict::Held: return 2;
        case radio::Verdict::Unarmed: return 3;
        case radio::Verdict::Received: return 4;
        case radio::Verdict::Named: return 5;
        case radio::Verdict::BadCrc: return 6;
        case radio::Verdict::Unframed: return 7;
        case radio::Verdict::Miskeyed: return 8;
        case radio::Verdict::Undecoded: return 9;
        case radio::Verdict::Unsupported: return 10;
        case radio::Verdict::Unattempted: return 11;
    }
    return kUnpinned;
}

uint8_t wire(model::Source value) {
    switch (value) {
        case model::Source::AdslDirect: return 0;
        case model::Source::AdslUplink: return 1;
        case model::Source::Alptas: return 2;
        case model::Source::Own: return 3;
    }
    return kUnpinned;
}

uint8_t wire(model::Band value) {
    switch (value) {
        case model::Band::M: return 0;
        case model::Band::O: return 1;
    }
    return kUnpinned;
}

uint8_t wire(timing::SlotState value) {
    switch (value) {
        case timing::SlotState::UplinkRxO: return 0;
        case timing::SlotState::SwitchOtoM: return 1;
        case timing::SlotState::Slot0: return 2;
        case timing::SlotState::Hop: return 3;
        case timing::SlotState::Slot1: return 4;
        case timing::SlotState::SwitchMtoO: return 5;
    }
    return kUnpinned;
}

uint8_t wire(diagnostics::Refusal value) {
    switch (value) {
        case diagnostics::Refusal::None: return 0;
        case diagnostics::Refusal::OverBudget: return 1;
        case diagnostics::Refusal::Unarmed: return 2;
        case diagnostics::Refusal::Unsettled: return 3;
        case diagnostics::Refusal::OffSchedule: return 4;
    }
    return kUnpinned;
}

uint8_t wire(diagnostics::SwitchKind value) {
    switch (value) {
        case diagnostics::SwitchKind::Retune: return 0;
        case diagnostics::SwitchKind::Hop: return 1;
        case diagnostics::SwitchKind::ToOBand: return 2;
        case diagnostics::SwitchKind::ToMBand: return 3;
        case diagnostics::SwitchKind::Wake: return 4;
    }
    return kUnpinned;
}

uint8_t wire(flight::FlightState value) {
    switch (value) {
        case flight::FlightState::Unknown: return 0;
        case flight::FlightState::Ground: return 1;
        case flight::FlightState::Airborne: return 2;
    }
    return kUnpinned;
}

uint8_t wire(power::PowerLevel value) {
    switch (value) {
        case power::PowerLevel::Unknown: return 0;
        case power::PowerLevel::Normal: return 1;
        case power::PowerLevel::Critical: return 2;
        case power::PowerLevel::Flat: return 3;
        case power::PowerLevel::Low: return 4;
    }
    return kUnpinned;
}

uint8_t wire(power::ChargeCondition value) {
    switch (value) {
        case power::ChargeCondition::Unknown: return 0;
        case power::ChargeCondition::Ok: return 1;
        case power::ChargeCondition::TooCold: return 2;
        case power::ChargeCondition::TooHot: return 3;
    }
    return kUnpinned;
}

uint8_t wire(events::Contact value) {
    switch (value) {
        case events::Contact::Button: return 0;
        case events::Contact::Pad: return 1;
    }
    return kUnpinned;
}

uint8_t wire(diagnostics::LinkAction value) {
    switch (value) {
        case diagnostics::LinkAction::Up: return 0;
        case diagnostics::LinkAction::Down: return 1;
        case diagnostics::LinkAction::ClaimTaken: return 2;
        case diagnostics::LinkAction::ClaimReleased: return 3;
        case diagnostics::LinkAction::Received: return 4;
        case diagnostics::LinkAction::Sent: return 5;
        case diagnostics::LinkAction::Dropped: return 6;
    }
    return kUnpinned;
}

uint8_t wire(events::Endpoint value) {
    switch (value) {
        case events::Endpoint::Config: return 0;
        case events::Endpoint::Nmea: return 1;
        case events::Endpoint::Log: return 2;
    }
    return kUnpinned;
}

uint8_t wire(timing::DurableWriteVerdict value) {
    switch (value) {
        case timing::DurableWriteVerdict::Idle: return 0;
        case timing::DurableWriteVerdict::Hold: return 1;
        case timing::DurableWriteVerdict::Place: return 2;
        case timing::DurableWriteVerdict::Forced: return 3;
    }
    return kUnpinned;
}

uint8_t wire(power::DurableWrite value) {
    switch (value) {
        case power::DurableWrite::Settings: return 0;
        case power::DurableWrite::FlightRecord: return 1;
    }
    return kUnpinned;
}

uint8_t wire(traffic::Level value) {
    switch (value) {
        case traffic::Level::None: return 0;
        case traffic::Level::Advisory: return 1;
    }
    return kUnpinned;
}

template <class E>
void codes_pinned(int count) {
    int found = 0;
    for (int value = 0; value < 256; value++) {
        const uint8_t code = wire(static_cast<E>(value));
        if (code == kUnpinned) continue;
        CHECK(code == value);
        CHECK(value < count);
        found++;
    }
    CHECK(found == count);
}

}  // namespace

TEST_CASE(
    "diagnostics ordinals: radio::Verdict's codes are the burst verdict byte, and a code changed "
    "here "
    "changes VERDICT in scripts/blip_records.py and the schema's verdict enum with it") {
    codes_pinned<radio::Verdict>(12);
}

TEST_CASE(
    "diagnostics ordinals: power::ResetReason's codes are the boot reset byte, and a code changed "
    "here changes RESET in scripts/blip_records.py and the schema's reset enum with it") {
    codes_pinned<power::ResetReason>(10);
}

TEST_CASE(
    "diagnostics ordinals: dfu::ImageState's codes are the boot image_state byte, and a code "
    "changed "
    "here changes IMAGE_STATE in scripts/blip_records.py and the schema's image_state enum") {
    codes_pinned<dfu::ImageState>(4);
}

TEST_CASE(
    "diagnostics ordinals: diagnostics::Profile's codes are the boot profile byte, and a code "
    "changed here "
    "changes PROFILE in scripts/blip_records.py and the schema's profile enum with it") {
    codes_pinned<diagnostics::Profile>(3);
}

TEST_CASE(
    "diagnostics ordinals: gnss::FixReject's codes are the gnss reject byte, and a code changed "
    "here "
    "changes REJECT in scripts/blip_records.py and the schema's reject enum with it") {
    codes_pinned<gnss::FixReject>(7);
}

TEST_CASE(
    "diagnostics ordinals: gnss::Stage's codes are the gnss stage byte, and a code changed here "
    "changes STAGE in scripts/blip_records.py and the schema's stage enum with it") {
    codes_pinned<gnss::Stage>(4);
}

TEST_CASE(
    "diagnostics ordinals: model::Source's codes are the burst and traffic source byte, and a code "
    "changed here changes SOURCE in scripts/blip_records.py and the schema's source enum") {
    codes_pinned<model::Source>(4);
}

TEST_CASE(
    "diagnostics ordinals: model::Band's codes are the burst and dwell band byte, and a code "
    "changed "
    "here changes BAND in scripts/blip_records.py and the schema's band enum with it") {
    codes_pinned<model::Band>(2);
}

TEST_CASE(
    "diagnostics ordinals: timing::SlotState's codes are the dwell state byte, and a code changed "
    "here changes SLOT_STATE in scripts/blip_records.py and the schema's state enum with it") {
    codes_pinned<timing::SlotState>(6);
}

TEST_CASE(
    "diagnostics ordinals: diagnostics::Refusal's codes are the dwell refusal byte, and a code "
    "changed here "
    "changes REFUSAL in scripts/blip_records.py and the schema's refusal enum with it") {
    codes_pinned<diagnostics::Refusal>(5);
}

TEST_CASE(
    "diagnostics ordinals: diagnostics::SwitchKind's codes are the switch change byte, and a code "
    "changed "
    "here changes CHANGE in scripts/blip_records.py and the schema's change enum with it") {
    codes_pinned<diagnostics::SwitchKind>(5);
}

TEST_CASE(
    "diagnostics ordinals: flight::FlightState's codes are the flight declared and confirmed "
    "bytes, "
    "and a code changed here changes FLIGHT_STATE in blip_records.py and both schema enums") {
    codes_pinned<flight::FlightState>(3);
}

TEST_CASE(
    "diagnostics ordinals: power::PowerLevel's codes are the power level byte, and a code changed "
    "here changes POWER_LEVEL in scripts/blip_records.py and the schema's level enum") {
    codes_pinned<power::PowerLevel>(5);
}

TEST_CASE(
    "diagnostics ordinals: power::ChargeCondition's codes are the power charge byte, and a code "
    "changed here changes CHARGE in scripts/blip_records.py and the schema's charge enum") {
    codes_pinned<power::ChargeCondition>(4);
}

TEST_CASE(
    "diagnostics ordinals: events::Contact's codes are the contact byte, and a code changed here "
    "changes CONTACT in scripts/blip_records.py and the schema's contact enum with it") {
    codes_pinned<events::Contact>(2);
}

TEST_CASE(
    "diagnostics ordinals: diagnostics::LinkAction's codes are the link action byte, and a code "
    "changed "
    "here changes LINK_ACTION in scripts/blip_records.py and the schema's action enum") {
    codes_pinned<diagnostics::LinkAction>(7);
}

TEST_CASE(
    "diagnostics ordinals: events::Endpoint's codes are the link endpoint byte, and a code changed "
    "here changes ENDPOINT in scripts/blip_records.py and the schema's endpoint enum") {
    codes_pinned<events::Endpoint>(3);
}

TEST_CASE(
    "diagnostics ordinals: timing::DurableWriteVerdict's codes are the write placement byte, and a "
    "code changed here changes PLACEMENT in blip_records.py and the schema's placement enum") {
    codes_pinned<timing::DurableWriteVerdict>(4);
}

TEST_CASE(
    "diagnostics ordinals: power::DurableWrite's codes are the write kind byte, and a code changed "
    "here changes WRITE_KIND in scripts/blip_records.py and the schema's kind enum with it") {
    codes_pinned<power::DurableWrite>(2);
}

TEST_CASE(
    "diagnostics ordinals: traffic::Level's codes are the traffic and screen alarm byte, and a "
    "member added here raises the maximum the schema puts on alarm") {
    codes_pinned<traffic::Level>(2);
}
