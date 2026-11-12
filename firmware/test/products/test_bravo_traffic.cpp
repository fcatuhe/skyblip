// A Go paired with the Bravo stub folds Bravo's targets in, and says BRAVO LOST on health alone.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/annunciation/annunciator.h"
#include "core/flight/log_record.h"
#include "core/settings/address.h"
#include "core/store/sector.h"
#include "core/traffic/range.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/nearby.h"
#include "products/skyblip_go/pages/self_test.h"
#include "test/support/bravo_flight.h"
#include "test/support/glass_text.h"
#include "test/support/nmea_stream.h"
#include "test/support/tango.h"

using namespace skyblip;

namespace {

std::vector<flight::LogEvent> bravo_records(Rig& rig) {
    std::vector<flight::LogEvent> out;
    platform::host::FlashRegion& flash = rig.platform.log_flash();
    std::vector<uint8_t> raw(flash.sector_bytes());
    for (uint32_t sector = 0; sector < flash.sector_count(); sector++) {
        REQUIRE(is_ok(flash.read(sector * flash.sector_bytes(), raw.data(), flash.sector_bytes())));
        store::SectorHeader header{};
        if (store::decode_sector_header(raw.data(), header) != Status::Ok) continue;
        if (header.owner != store::SectorOwner::Flights) continue;
        for (uint32_t slot = 0; slot < flight::kLogSlotsPerSector; slot++) {
            flight::LogEvent event{};
            if (flight::decode_log_event(raw.data() + flight::log_record_offset(slot),
                                         header.session_id, event) == Status::Ok &&
                event.kind == flight::LogKind::Bravo)
                out.push_back(event);
        }
    }
    return out;
}

std::string last_status(Rig& rig) {
    const auto& sent = rig.platform.link().sent;
    for (auto it = sent.rbegin(); it != sent.rend(); ++it)
        if (it->endpoint == events::Endpoint::Config &&
            link_text(it->bytes).rfind("{\"command\":\"status\"", 0) == 0)
            return link_text(it->bytes);
    return std::string();
}

std::string last_pflaa(Rig& rig) {
    std::string found;
    for (const std::string& sentence : sentences(rig))
        if (sentence.rfind("$PFLAA,", 0) == 0) found = sentence;
    return found;
}

}  // namespace

TEST_CASE("bravo traffic: Bravo's target enters the table at the instants Bravo heard it") {
    Flight flight(
        {{edge_ms_of(2) + 500, level_target(2, 200, 1500,
                                            ",\"callsign\":\"DLH4AB\",\"callsign_heard_s\":" +
                                                std::to_string(utc_of(1)))}});
    flight.fly(4);

    const traffic::Target* target = flight.target();
    REQUIRE(target != nullptr);
    CHECK(target->observation.source == model::Source::Adsb);
    CHECK(target->observation.received.at_s == utc_of(2));
    CHECK(target->observation.at_ms == edge_ms_of(2) + 200);
    CHECK(std::string(flight.state().callsigns.find(settings::kIcaoAddressMappingTable, kTarget)) ==
          "DLH4AB");
    CHECK(flight.tango.service().unplaced() == 0);
}

// One merged stream: the Go's own $PFLAA carries Bravo's target as ADS-B, Source 1 (#175).
TEST_CASE("bravo traffic: the phone hears Bravo's target in the Go's own $PFLAA, as Source 1") {
    Flight flight(named_target_every_second(2, 6));
    flight.fly(5);

    const std::vector<std::string> values = fields(last_pflaa(flight.go()));
    REQUIRE(values.size() >= 14);
    CHECK(std::stoi(values[2]) > 1300);
    CHECK(std::stoi(values[2]) < 1700);
    CHECK(values[5] == "1");
    CHECK(values[6] == "4459A9!DLH4AB");
    CHECK(values[13] == "1");
}

TEST_CASE("bravo traffic: nearby lists Bravo's target under B, and the radar marks a live Bravo") {
    Flight flight(named_target_every_second(2, 8));
    flight.fly(3);
    CHECK(flight.state().bravo == comms::BravoState::Live);
    CHECK(flight.reads("BRAVO"));
    CHECK_FALSE(flight.reads("BRAVO LOST", 2));

    flight.go().tap(flight.tango.now_ms);
    REQUIRE(flight.go().product.screen().page() == go::Page::Nearby);
    flight.go().run(flight.tango.now_ms, flight.tango.now_ms + 1100);
    CHECK(flight.reads("B DLH4AB", go::kNearbyScale));
}

// An empty sky is a healthy one: Bravo's health, not its traffic, says it is there.
TEST_CASE("bravo traffic: a quiet sky with Bravo's health coming is no BRAVO LOST and no tone") {
    Flight flight;
    flight.fly(3);
    flight.tones.clear();
    flight.fly(17);
    CHECK(flight.state().bravo == comms::BravoState::Live);
    CHECK_FALSE(flight.reads("BRAVO LOST", 2));
    CHECK(flight.tones.empty());
    CHECK(last_status(flight.go()).find("\"bravo\":\"live\"") != std::string::npos);
}

TEST_CASE("bravo traffic: a lost health says BRAVO LOST once, with one tone, until it comes back") {
    Flight flight;
    flight.fly(3);
    flight.tones.clear();
    flight.tango.air.set_in_range(false);
    flight.tango.air.drop();
    flight.fly(2);
    CHECK(flight.state().bravo == comms::BravoState::Live);

    flight.fly(4);
    CHECK(flight.state().bravo == comms::BravoState::LinkDown);
    CHECK(flight.reads("BRAVO LOST", 2));
    CHECK(flight.reads("LINK DOWN"));
    CHECK_FALSE(flight.reads("BRAVO"));
    CHECK(flight.tones == std::vector<uint16_t>{annunciation::kNoteC7Hz});
    CHECK(last_status(flight.go()).find("\"bravo\":\"link_down\"") != std::string::npos);

    flight.tango.air.set_in_range(true);
    flight.fly(5);
    CHECK(flight.state().bravo == comms::BravoState::Live);
    CHECK_FALSE(flight.reads("BRAVO LOST", 2));
    CHECK(flight.tones.size() == 1);
    CHECK(last_status(flight.go()).find("\"bravo\":\"live\"") != std::string::npos);
}

TEST_CASE("bravo traffic: a deaf receiver and a Bravo without UTC are BRAVO LOST, and say which") {
    Flight flight;
    flight.fly(3);
    flight.tones.clear();
    flight.tango.bravo.health.listening = false;
    flight.fly(2);
    CHECK(flight.state().bravo == comms::BravoState::ReceiverDown);
    CHECK(flight.reads("BRAVO LOST", 2));
    CHECK(flight.reads("RECEIVER DOWN"));
    CHECK(last_status(flight.go()).find("\"bravo\":\"receiver_down\"") != std::string::npos);

    flight.tango.bravo.health.listening = true;
    flight.tango.bravo.health.utc_valid = false;
    flight.fly(2);
    CHECK(flight.state().bravo == comms::BravoState::NoUtc);
    CHECK(flight.reads("NO UTC"));
    CHECK(flight.tones.size() == 1);

    flight.go().show(flight.tango.now_ms, go::Page::SelfTest);
    flight.go().run(flight.tango.now_ms, flight.tango.now_ms + 1100);
    CHECK(reads_in(flight.glass(), "BRAVO 3F2A", 0, go::kSelfTestFirstRowY, go::kGlassWidth,
                   go::kGlassHeight));
    CHECK(reads_in(flight.glass(), "NO UTC", 0, go::kSelfTestFirstRowY, go::kGlassWidth,
                   go::kGlassHeight));
}

TEST_CASE("bravo traffic: a Bravo that loses the UTC it had is BRAVO LOST, with one tone") {
    Flight flight;
    flight.fly(3);
    REQUIRE(flight.state().bravo == comms::BravoState::Live);
    flight.tones.clear();
    flight.tango.bravo.health.utc_valid = false;
    flight.fly(2);
    CHECK(flight.state().bravo == comms::BravoState::NoUtc);
    CHECK(flight.reads("BRAVO LOST", 2));
    CHECK(flight.reads("NO UTC"));
    CHECK(flight.tones == std::vector<uint16_t>{annunciation::kNoteC7Hz});
}

TEST_CASE("bravo traffic: a Bravo that reboots after having UTC is lost until it has it again") {
    Flight flight;
    flight.fly(3);
    REQUIRE(flight.state().bravo == comms::BravoState::Live);
    flight.tones.clear();
    flight.tango.bravo.health.utc_valid = false;
    flight.tango.air.reboot_peripheral(flight.tango.bravo.reboot());
    flight.tango.air.drop();
    flight.fly(5);
    REQUIRE(flight.tango.service().link() == go::BravoLink::Up);
    CHECK(flight.state().bravo == comms::BravoState::NoUtc);
    CHECK(flight.reads("BRAVO LOST", 2));
    CHECK(flight.tones == std::vector<uint16_t>{annunciation::kNoteC7Hz});

    flight.tango.bravo.health.utc_valid = true;
    flight.fly(2);
    CHECK(flight.state().bravo == comms::BravoState::Live);
}

TEST_CASE("bravo traffic: a dropped pin forgets Bravo's UTC, so the next Bravo without it waits") {
    Flight flight;
    flight.fly(3);
    REQUIRE(flight.state().bravo == comms::BravoState::Live);
    SUBCASE("unpaired on the Go") {
        flight.go().show(flight.tango.now_ms, go::Page::Bravo);
        flight.go().press(flight.tango.now_ms);
        REQUIRE(flight.go().product.screen().bravo_page().unpairing());
        flight.go().press(flight.tango.now_ms);
        flight.tango.run_for(5000);
    }
    SUBCASE("forgotten by Bravo") {
        flight.tango.bravo.health.paired = false;
        flight.tango.run_for(2000);
        flight.tango.bravo.health.paired = true;
        REQUIRE(flight.go().product.screen().notice() == go::ScreenService::Notice::BravoForgot);
        flight.go().tap(flight.tango.now_ms);
        REQUIRE_FALSE(flight.go().product.screen().noticing());
    }
    REQUIRE_FALSE(go::paired(flight.go().settings()));

    flight.tones.clear();
    flight.tango.bravo.health.utc_valid = false;
    flight.tango.pair();
    flight.tango.run_for(3000);
    REQUIRE(flight.tango.service().link() == go::BravoLink::Up);
    CHECK(flight.state().bravo == comms::BravoState::Waiting);
    CHECK(flight.tones.empty());
}

TEST_CASE("bravo traffic: an unpaired Go never says BRAVO LOST, nor sends a bravo key") {
    Flight flight({}, /*paired=*/false);
    flight.fly(3);
    flight.tones.clear();
    flight.fly(17);
    CHECK(flight.state().bravo == comms::BravoState::Unpaired);
    CHECK_FALSE(flight.reads("BRAVO LOST", 2));
    CHECK_FALSE(flight.reads("BRAVO"));
    CHECK(flight.tones.empty());
    CHECK(last_status(flight.go()).find("bravo") == std::string::npos);
}

TEST_CASE("bravo traffic: the flight log records the Bravo lost in flight, and back") {
    Flight flight;
    flight.fly(20);
    REQUIRE(flight.go().product.flight_log().recording());
    flight.tango.air.set_in_range(false);
    flight.tango.air.drop();
    flight.fly(6);
    flight.tango.air.set_in_range(true);
    flight.fly(5);
    REQUIRE(flight.state().bravo == comms::BravoState::Live);
    flight.go().seconds(flight.tango.now_ms, 40, 0, kAltitudeMslM);
    REQUIRE_FALSE(flight.go().product.flight_log().recording());

    const std::vector<flight::LogEvent> logged = bravo_records(flight.go());
    REQUIRE(logged.size() == 2);
    CHECK(logged[0].bravo == static_cast<uint8_t>(comms::BravoState::LinkDown));
    CHECK(logged[1].bravo == static_cast<uint8_t>(comms::BravoState::Live));
    CHECK(logged[0].utc_s < logged[1].utc_s);
}

TEST_CASE("bravo traffic: a stale observation forwarded late is held to its own lease, no longer") {
    Flight flight({{edge_ms_of(5) + 100, level_target(1, 0, 1500)},
                   {edge_ms_of(6) + 100, level_target(1, 0, 1500)}});
    flight.fly(7);
    const traffic::Target* held = flight.target();
    REQUIRE(held != nullptr);
    CHECK(held->observation.received.at_s == utc_of(1));
    CHECK(held->observation.at_ms == edge_ms_of(1));

    // Heard at second 1, an airborne target's six-second lease ends at second 8, not at 12.
    flight.fly(1);
    CHECK(flight.target() != nullptr);
    flight.fly(1);
    CHECK(flight.target() == nullptr);
}

TEST_CASE("bravo traffic: a pressure-only target has a height while the barometer is live") {
    std::vector<BravoStub::Recorded> recording;
    for (uint32_t second = 2; second < 14; second++)
        recording.push_back({edge_ms_of(second) + 300,
                             "\"address\":" + std::to_string(kTarget) + "," +
                                 heard_at(second, 100, 1500) +
                                 ",\"pressure_altitude_ft\":4500,\"pressure_altitude_age_ms\":0"});
    Flight flight(recording, kBarometerByHand);
    for (int second = 0; second < 5; second++) {
        flight.go().push_barometer(100000, flight.tango.now_ms);
        flight.fly(1);
    }
    traffic::RangeRow rows[4];
    REQUIRE(traffic::rank_by_range(flight.state().traffic, flight.state().callsigns,
                                   flight.state().own, rows, 4) == 1);
    CHECK(rows[0].up_valid);
    // 4500 ft is 1371.6 m of pressure altitude, and own-ship's barometer reads 1000 m.
    CHECK(rows[0].up_m == doctest::Approx(372).epsilon(0.01));

    flight.fly(5);
    CHECK_FALSE(flight.state().own.pressure_altitude_valid);
    REQUIRE(traffic::rank_by_range(flight.state().traffic, flight.state().callsigns,
                                   flight.state().own, rows, 4) == 1);
    CHECK_FALSE(rows[0].up_valid);
}

TEST_CASE("bravo traffic: a paired Go waits for its Bravo after boot, then says it is lost") {
    Tango tango;
    tango.pair_and_store();
    uint8_t blob[64] = {};
    size_t bytes = 0;
    REQUIRE(tango.go.platform.key_value().read("settings", blob, sizeof(blob), bytes) ==
            Status::Ok);

    Rig rebooted;
    REQUIRE(rebooted.platform.key_value().write("settings", blob, bytes) == Status::Ok);
    REQUIRE(rebooted.setup() == Status::Ok);
    REQUIRE(go::paired(rebooted.settings()));
    rebooted.run(0, go::BravoService::kBootSettleMs - 100);
    CHECK(rebooted.state().bravo == comms::BravoState::Waiting);
    rebooted.run(go::BravoService::kBootSettleMs, go::BravoService::kBootSettleMs + 100);
    CHECK(rebooted.state().bravo == comms::BravoState::LinkDown);
}

// ports::Clock's milliseconds come round every 49.7 days, and an old health must not come back.
TEST_CASE("bravo traffic: a lost Bravo stays lost when the uptime counter comes round again") {
    Flight flight;
    flight.fly(3);
    REQUIRE(flight.state().bravo == comms::BravoState::Live);
    const uint32_t live_ms = flight.tango.now_ms;
    flight.tango.air.set_in_range(false);
    flight.tango.air.drop();
    flight.fly(5);
    REQUIRE(flight.state().bravo == comms::BravoState::LinkDown);

    // INFO: fc 12nov26 live_ms + 2^32 is live_ms again on a 32-bit millisecond counter
    flight.go().run(live_ms, live_ms + 500);
    CHECK(flight.state().bravo == comms::BravoState::LinkDown);
}
