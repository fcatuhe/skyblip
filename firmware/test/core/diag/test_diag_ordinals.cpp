// Every enum a diagnostics payload writes as a byte, pinned to the code the corpus holds.
#include "core/diag/payload.h"
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

uint8_t wire(dfu::ImageState value) {
    switch (value) {
        case dfu::ImageState::Confirmed: return 0;
        case dfu::ImageState::Probation: return 1;
        case dfu::ImageState::Reverted: return 2;
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

uint8_t wire(radio::Event value) {
    switch (value) {
        case radio::Event::Transmitted: return 0;
        case radio::Event::Lost: return 1;
        case radio::Event::Held: return 2;
        case radio::Event::Unarmed: return 3;
        case radio::Event::Received: return 4;
        case radio::Event::Named: return 5;
        case radio::Event::BadCrc: return 6;
        case radio::Event::Unframed: return 7;
        case radio::Event::Miskeyed: return 8;
        case radio::Event::Undecoded: return 9;
        case radio::Event::Unsupported: return 10;
        case radio::Event::Unattempted: return 11;
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

uint8_t wire(diag::Refusal value) {
    switch (value) {
        case diag::Refusal::None: return 0;
        case diag::Refusal::OverBudget: return 1;
        case diag::Refusal::Unarmed: return 2;
        case diag::Refusal::Unsettled: return 3;
        case diag::Refusal::OffSchedule: return 4;
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
        case power::PowerLevel::Low: return 2;
        case power::PowerLevel::Cutoff: return 3;
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

uint8_t wire(diag::LinkAction value) {
    switch (value) {
        case diag::LinkAction::Up: return 0;
        case diag::LinkAction::Down: return 1;
        case diag::LinkAction::ClaimTaken: return 2;
        case diag::LinkAction::ClaimReleased: return 3;
        case diag::LinkAction::Received: return 4;
        case diag::LinkAction::Sent: return 5;
        case diag::LinkAction::Dropped: return 6;
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
    "diag ordinals: radio::Event's codes are the burst verdict byte, and a code changed here "
    "changes VERDICT in scripts/blip_records.py and the schema's verdict enum with it") {
    codes_pinned<radio::Event>(12);
}

TEST_CASE(
    "diag ordinals: power::ResetReason's codes are the boot reset byte, and a code changed "
    "here changes RESET in scripts/blip_records.py and the schema's reset enum with it") {
    codes_pinned<power::ResetReason>(10);
}

TEST_CASE(
    "diag ordinals: dfu::ImageState's codes are the boot image_state byte, and a code changed "
    "here changes IMAGE_STATE in scripts/blip_records.py and the schema's image_state enum") {
    codes_pinned<dfu::ImageState>(3);
}

TEST_CASE(
    "diag ordinals: gnss::FixReject's codes are the gnss reject byte, and a code changed here "
    "changes REJECT in scripts/blip_records.py and the schema's reject enum with it") {
    codes_pinned<gnss::FixReject>(7);
}

TEST_CASE(
    "diag ordinals: gnss::Stage's codes are the gnss stage byte, and a code changed here "
    "changes STAGE in scripts/blip_records.py and the schema's stage enum with it") {
    codes_pinned<gnss::Stage>(4);
}

TEST_CASE(
    "diag ordinals: model::Source's codes are the burst and traffic source byte, and a code "
    "changed here changes SOURCE in scripts/blip_records.py and the schema's source enum") {
    codes_pinned<model::Source>(4);
}

TEST_CASE(
    "diag ordinals: model::Band's codes are the burst and dwell band byte, and a code changed "
    "here changes BAND in scripts/blip_records.py and the schema's band enum with it") {
    codes_pinned<model::Band>(2);
}

TEST_CASE(
    "diag ordinals: timing::SlotState's codes are the dwell state byte, and a code changed "
    "here changes SLOT_STATE in scripts/blip_records.py and the schema's state enum with it") {
    codes_pinned<timing::SlotState>(6);
}

TEST_CASE(
    "diag ordinals: diag::Refusal's codes are the dwell refusal byte, and a code changed here "
    "changes REFUSAL in scripts/blip_records.py and the schema's refusal enum with it") {
    codes_pinned<diag::Refusal>(5);
}

TEST_CASE(
    "diag ordinals: flight::FlightState's codes are the flight declared and confirmed bytes, "
    "and a code changed here changes FLIGHT_STATE in blip_records.py and both schema enums") {
    codes_pinned<flight::FlightState>(3);
}

TEST_CASE(
    "diag ordinals: power::PowerLevel's codes are the power level byte, and a code changed "
    "here changes POWER_LEVEL in scripts/blip_records.py and the schema's level enum") {
    codes_pinned<power::PowerLevel>(4);
}

TEST_CASE(
    "diag ordinals: power::ChargeCondition's codes are the power charge byte, and a code "
    "changed here changes CHARGE in scripts/blip_records.py and the schema's charge enum") {
    codes_pinned<power::ChargeCondition>(4);
}

TEST_CASE(
    "diag ordinals: events::Contact's codes are the contact byte, and a code changed here "
    "changes CONTACT in scripts/blip_records.py and the schema's contact enum with it") {
    codes_pinned<events::Contact>(2);
}

TEST_CASE(
    "diag ordinals: diag::LinkAction's codes are the link action byte, and a code changed "
    "here changes LINK_ACTION in scripts/blip_records.py and the schema's action enum") {
    codes_pinned<diag::LinkAction>(7);
}

TEST_CASE(
    "diag ordinals: events::Endpoint's codes are the link endpoint byte, and a code changed "
    "here changes ENDPOINT in scripts/blip_records.py and the schema's endpoint enum") {
    codes_pinned<events::Endpoint>(3);
}

TEST_CASE(
    "diag ordinals: timing::DurableWriteVerdict's codes are the write placement byte, and a "
    "code changed here changes PLACEMENT in blip_records.py and the schema's placement enum") {
    codes_pinned<timing::DurableWriteVerdict>(4);
}

TEST_CASE(
    "diag ordinals: power::DurableWrite's codes are the write kind byte, and a code changed "
    "here changes WRITE_KIND in scripts/blip_records.py and the schema's kind enum with it") {
    codes_pinned<power::DurableWrite>(2);
}

TEST_CASE(
    "diag ordinals: traffic::Level's codes are the traffic and screen alarm byte, and a "
    "member added here raises the maximum the schema puts on alarm") {
    codes_pinned<traffic::Level>(2);
}
