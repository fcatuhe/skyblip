// The Go proposes the FLARM it hears loudest aboard as the aircraft's own, on the ground only.
#include <cstdint>
#include <string>

#include "core/events/rf.h"
#include "core/model/band.h"
#include "core/protocol/adsl.h"
#include "core/protocol/air.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "products/skyblip_go/own_flarm.h"
#include "products/skyblip_go/pages/aircraft.h"
#include "products/skyblip_go/pages/menu.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl/physical.h"
#include "protocols/common/manchester.h"
#include "test/support/glass_text.h"
#include "test/support/log_transfer.h"
#include "test/support/menu_rig.h"

using namespace skyblip;

namespace {

constexpr uint32_t kOwnFlarm = 0xDD1234;
constexpr uint32_t kNeighbourFlarm = 0xDD5678;
constexpr int8_t kCockpitDbm = -20;
constexpr int8_t kNeighbourDbm = -60;
constexpr int32_t kParkedAltitudeM = 500;

// As m_band_rig.h hands a burst over, at the level the case names.
void hear_device(Rig& rig, uint32_t address, uint8_t address_mapping_table, int8_t rssi_dbm,
                 int32_t north_1e7) {
    model::Ownship emitter = rig.state().own;
    emitter.latitude_1e7 += north_1e7;
    adsl::Packet packet{};
    REQUIRE(protocol::from_own(packet, emitter, address, address_mapping_table,
                               /*aircraft_category=*/4));
    const uint8_t bytes = packet.data_bytes();
    packet.scramble(bytes);
    packet.set_crc(bytes);
    uint8_t chips[protocol::kTxChipBytes];
    const size_t chip_bytes =
        wire::manchester_frame(adsl::sync_word(bytes), packet.data, bytes, chips);
    events::RfEvent event{};
    event.type = events::RfEventType::RxDone;
    event.band = model::Band::M;
    event.rssi_dbm = rssi_dbm;
    event.rssi_valid = true;
    event.bytes = models::Sx1262::deliver_after_sync(
        chips, static_cast<uint16_t>(chip_bytes), protocol::kSharedSync, protocol::kSharedSyncBits,
        event.data.data(), protocol::kRxChipBytes);
    rig.product.bus().rf.push(event);
}

// A second each: our own device beside the seat, and a neighbour's FLARM across the apron.
void listen(Rig& rig, uint32_t seconds, uint32_t own = kOwnFlarm,
            uint8_t own_table = adsl::kAddressTableFlarm, int32_t speed_mm_s = 0,
            int32_t altitude_m = kParkedAltitudeM) {
    for (uint32_t i = 0; i < seconds; i++) {
        hear_device(rig, own, own_table, kCockpitDbm, 10);
        hear_device(rig, kNeighbourFlarm, adsl::kAddressTableFlarm, kNeighbourDbm, 2000);
        rig.seconds(1, speed_mm_s, altitude_m);
    }
}

uint32_t held_seconds() { return go::OwnFlarm::kLeadHoldMs / 1000 + 3; }

void parked_on_the_radar(Rig& rig) {
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds(5, 0, kParkedAltitudeM);
    REQUIRE(rig.state().flight.confirmed_state == flight::FlightState::Ground);
    rig.long_touch();
    REQUIRE(rig.product.screen().page() == go::Page::Radar);
    REQUIRE_FALSE(rig.product.screen().noticing());
}

bool glass_reads(Rig& rig, const char* text, int scale, bool ink = true) {
    return reads_in(rig.product.screen().framebuffer(), text, 0, 0, go::kGlassWidth,
                    go::kGlassHeight, scale, ink);
}

}  // namespace

TEST_CASE("flarm detection: the FLARM aboard is proposed once, and a press stores it" *
          doctest::test_suite("main")) {
    Rig rig;
    parked_on_the_radar(rig);
    listen(rig, held_seconds());
    REQUIRE(rig.state().aboard.has_value());
    CHECK(rig.state().aboard->address == kOwnFlarm);
    REQUIRE(rig.product.screen().noticing());
    CHECK(rig.product.screen().notice() == go::ScreenService::Notice::NewFlarm);
    rig.run_for(3000);
    CHECK(glass_reads(rig, "DD1234", go::kCallsignScale));
    CHECK(glass_reads(rig, go::kFlarmNoticeTitle, go::kFrameScale));

    rig.press();
    rig.run_for(500);
    CHECK_FALSE(rig.product.screen().noticing());
    CHECK(rig.settings().flarm_id == kOwnFlarm);

    // Once a boot: another FLARM later is the FLARM menu's to propose, not a notice.
    listen(rig, held_seconds(), 0xDD9999);
    CHECK_FALSE(rig.product.screen().noticing());
}

TEST_CASE("flarm detection: the stored FLARM is never traffic, and its neighbour still is" *
          doctest::test_suite("main")) {
    Rig rig;
    parked_on_the_radar(rig);
    listen(rig, 3);
    CHECK(rig.state().traffic.find(adsl::kAddressTableFlarm, kOwnFlarm) >= 0);

    rig.settings().flarm_id = kOwnFlarm;
    listen(rig, 3);
    CHECK(rig.state().traffic.find(adsl::kAddressTableFlarm, kOwnFlarm) < 0);
    CHECK(rig.state().traffic.find(adsl::kAddressTableFlarm, kNeighbourFlarm) >= 0);
}

// A page beside the Go proposes the same FLARM, and a set of another is refused.
TEST_CASE("flarm detection: the Go's status names the FLARM aboard, and a set must match it") {
    Rig rig;
    parked_on_the_radar(rig);
    listen(rig, held_seconds());
    rig.send("{\"command\":\"status\"}");
    rig.run_for(200);
    const std::string status = rig.last_on(events::Endpoint::Config);
    REQUIRE(field(status, "command") == "status");
    CHECK(field(status, "own_flarm") == std::to_string(kOwnFlarm));

    rig.send("{\"command\":\"set\",\"flarm_id\":14505592}");
    rig.run_for(200);
    CHECK(field(rig.last_on(events::Endpoint::Config), "reason") == "not_own_flarm");
    CHECK(rig.settings().flarm_id == go::kFlarmIdUnset);
}

// An ICAO address aboard is the transponder's, which the ADS-B row covers.
TEST_CASE("flarm detection: a device aboard under an ICAO address is no FLARM ID to store") {
    Rig rig;
    parked_on_the_radar(rig);
    listen(rig, held_seconds(), 0x3A4B5C, adsl::kAddressTableIcao);
    REQUIRE(rig.state().aboard.has_value());
    CHECK(rig.state().aboard->address_mapping_table == adsl::kAddressTableIcao);
    CHECK_FALSE(rig.product.screen().noticing());

    open_menu(rig);
    focus_on(rig, go::MenuRow::FlarmHeard);
    rig.run_for(3000);
    CHECK(glass_reads(rig, "ICAO ADDRESS", go::kMenuScale, false));
    change(rig);
    CHECK(rig.settings().flarm_id == go::kFlarmIdUnset);
}

TEST_CASE("flarm detection: the FLARM menu reads what the Go hears, and a press stores it") {
    Rig rig;
    parked_on_the_radar(rig);
    listen(rig, held_seconds());
    REQUIRE(rig.product.screen().noticing());
    rig.tap();
    REQUIRE_FALSE(rig.product.screen().noticing());
    CHECK(rig.settings().flarm_id == go::kFlarmIdUnset);

    open_menu(rig);
    focus_on(rig, go::MenuRow::FlarmHeard);
    rig.run_for(3000);
    CHECK(glass_reads(rig, "DD1234", go::kCallsignScale, false));
    CHECK(glass_reads(rig, go::kFlarmIdLabel, go::kMenuScale, false));

    change(rig);
    CHECK(rig.settings().flarm_id == kOwnFlarm);
    CHECK(rig.product.screen().editor().focus() == go::MenuRow::Flarm);
    run_past_the_write_settle(rig);
    go::Settings stored{};
    REQUIRE(stored_settings(rig, stored));
    CHECK(stored.flarm_id == kOwnFlarm);
}

// Two quick presses, OPEN then SET, once stored a block the glass had not drawn yet.
TEST_CASE("flarm detection: a press on a block not yet on the glass stores nothing") {
    Rig rig;
    parked_on_the_radar(rig);
    listen(rig, held_seconds());
    rig.tap();
    open_menu(rig);
    walk_to(rig, go::MenuRow::Aircraft);
    rig.tap();
    REQUIRE(rig.product.screen().editor().focus() == go::MenuRow::Flarm);
    rig.press();
    REQUIRE(rig.product.screen().editor().flarm());
    rig.press();
    CHECK(rig.settings().flarm_id == go::kFlarmIdUnset);

    rig.run_for(3000);
    change(rig);
    CHECK(rig.settings().flarm_id == kOwnFlarm);
}

// A wingman's FLARM aloft is as loud as our own, so the air proposes nothing.
TEST_CASE("flarm detection: in flight nothing is proposed, and the menu says it waits") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    listen(rig, 40, kOwnFlarm, adsl::kAddressTableFlarm, 30000, 900);
    REQUIRE(rig.state().flight.confirmed_state == flight::FlightState::Airborne);
    CHECK_FALSE(rig.state().aboard.has_value());
    CHECK_FALSE(rig.product.screen().noticing());

    open_menu(rig);
    focus_on(rig, go::MenuRow::FlarmHeard);
    rig.seconds(3, 30000, 900);
    CHECK(glass_reads(rig, "IN FLIGHT", go::kMenuScale, false));
    change(rig);
    CHECK(rig.settings().flarm_id == go::kFlarmIdUnset);
}

// Clearing takes the question, read on the glass, then the press.
TEST_CASE("flarm detection: a stored FLARM ID is erased only from the question on the glass") {
    Rig rig;
    parked_on_the_radar(rig);
    rig.settings().flarm_id = kOwnFlarm;
    open_menu(rig);
    focus_on(rig, go::MenuRow::FlarmId);
    change(rig);
    REQUIRE(rig.product.screen().editor().editing());
    while (rig.product.screen().editor().text()[0] != go::kCallsignBlank) move(rig);
    rig.press();
    REQUIRE(rig.product.screen().editor().clearing());
    rig.press();
    CHECK(rig.product.screen().editor().clearing());
    CHECK(rig.settings().flarm_id == kOwnFlarm);

    rig.run_for(3000);
    CHECK(glass_reads(rig, go::kFlarmClearOwner, go::kFrameScale));
    change(rig);
    CHECK_FALSE(rig.product.screen().editor().clearing());
    CHECK(rig.settings().flarm_id == go::kFlarmIdUnset);
}
