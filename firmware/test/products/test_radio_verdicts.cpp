// Three facts the tape spelled DEC alike, told apart through the whole product.
#include <cstring>
#include <string>

#include "core/events/rf.h"
#include "core/model/aircraft.h"
#include "core/model/band.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/air.h"
#include "core/protocol/alptas.h"
#include "core/radio/log.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

model::AircraftObservation neighbour(const model::Ownship& own) {
    model::AircraftObservation observation{};
    observation.address = 0xC5D804;
    observation.address_mapping_table = 6;
    observation.aircraft_cat = 4;
    observation.flight_state = 2;
    observation.lat_1e7 = own.lat_1e7 + 10000;
    observation.lon_1e7 = own.lon_1e7;
    observation.alt_m = 900;
    observation.speed_q = 160;
    observation.track_c9 = 256;
    observation.speed_valid = true;
    observation.position_valid = true;
    return observation;
}

// A neighbour two metres away, as the chip hands one over: past the shared sync window.
void hear_mband(Rig& rig, uint32_t sync_word, const uint8_t* frame, uint8_t frame_bytes,
                int dead_chip_byte = -1) {
    uint8_t chips[protocol::kTxChipBytes];
    const size_t chip_len = protocol::encode_mband(sync_word, frame, frame_bytes, chips);
    if (dead_chip_byte >= 0) chips[dead_chip_byte] ^= 0x01;

    events::RfEvent event{};
    event.type = events::RfEventType::RxDone;
    event.band = model::Band::M;
    event.rssi_dbm = -15;
    event.rssi_valid = true;
    event.len = models::Sx1262::deliver_after_sync(chips, static_cast<uint16_t>(chip_len),
                                                   protocol::kSharedSync, protocol::kSharedSyncBits,
                                                   event.data.data(), protocol::kRxChipBytes);
    rig.product.bus().rf.push(event);
}

void hear_alptas(Rig& rig, const uint8_t* frame, int dead_chip_byte = -1) {
    hear_mband(rig, protocol::kAlptasSyncWord, frame, protocol::kAlptasFrameBytes, dead_chip_byte);
}

void hear_adsl(Rig& rig, protocol::AdslPacket& p) {
    p.scramble();
    p.set_crc();
    hear_mband(rig, protocol::kAdslSyncWord, p.Data, protocol::kAdslFrameBytes);
}

protocol::AdslPacket adsl_from_neighbour(const model::Ownship& own) {
    model::Ownship transmitter = own;
    transmitter.lat_1e7 += 10000;
    protocol::AdslPacket p{};
    protocol::from_own(p, transmitter, 0xC5D804, /*address_mapping_table=*/6, /*aircraft_cat=*/4);
    return p;
}

// A neighbour naming itself: OGN's payload 66, scrambled and checksummed as any ADS-L frame is.
void hear_callsign(Rig& rig, uint32_t address, uint8_t address_mapping_table,
                   const char* callsign) {
    protocol::AdslPacket p{};
    protocol::from_own_callsign(p, address, address_mapping_table, callsign);
    p.scramble();
    p.set_crc();
    hear_mband(rig, protocol::kAdslSyncWord, p.Data, protocol::kAdslFrameBytes);
}

const radio::Entry* heard_entry(Rig& rig) {
    const radio::Log& log = rig.state().radio_log;
    for (int i = 0; i < log.count(); i++) {
        const radio::Entry& entry = log.newest(i);
        if (entry.verdict != radio::Verdict::Transmitted && entry.verdict != radio::Verdict::Lost &&
            entry.verdict != radio::Verdict::Held && entry.verdict != radio::Verdict::Unarmed)
            return &entry;
    }
    return nullptr;
}

// Own-ship's own bursts are the other half of the tape, and a case about receptions ignores them.
radio::Verdict heard_verdict(Rig& rig) {
    const radio::Log& log = rig.state().radio_log;
    for (int i = 0; i < log.count(); i++) {
        const radio::Verdict verdict = log.newest(i).verdict;
        if (verdict != radio::Verdict::Transmitted && verdict != radio::Verdict::Lost &&
            verdict != radio::Verdict::Held && verdict != radio::Verdict::Unarmed)
            return verdict;
    }
    return radio::Verdict::Transmitted;
}

void fly(Rig& rig, uint32_t& t, uint32_t seconds) { rig.seconds(t, seconds, 25000, 900); }

void settle(Rig& rig, uint32_t& t) {
    rig.run(t, t + 200);
    t += 250;
}

}  // namespace

TEST_CASE("radio verdicts: a burst heard before own-ship has a fix is a wait, not a failure") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    settle(rig, t);
    REQUIRE_FALSE(rig.state().own.fix_valid);

    model::AircraftObservation observation = neighbour(rig.state().own);
    observation.lat_1e7 = 481234567;
    observation.lon_1e7 = 87654321;
    uint8_t frame[protocol::kAlptasFrameBytes];
    REQUIRE(protocol::alptas_encode(frame, observation, Rig::kUtcBase, 481000000, 87000000) ==
            Status::Ok);

    hear_alptas(rig, frame);
    settle(rig, t);

    CHECK(heard_verdict(rig) == radio::Verdict::Unattempted);
}

// SoftRF built with USE_INTERLEAVING sends Air V6 between its V7 frames, and we read only V7.
TEST_CASE("radio verdicts: a message type this firmware does not implement says so") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);

    const model::Ownship& own = rig.state().own;
    uint8_t frame[protocol::kAlptasFrameBytes];
    REQUIRE(protocol::alptas_encode(frame, neighbour(own), own.utc_s, own.lat_1e7, own.lon_1e7) ==
            Status::Ok);
    // The message type is the low nibble of byte 3, in clear, before any decrypt stage.
    frame[3] = static_cast<uint8_t>(frame[3] & 0xF0);
    protocol::alptas_set_crc(frame);

    hear_alptas(rig, frame);
    settle(rig, t);

    CHECK(heard_verdict(rig) == radio::Verdict::Unsupported);
}

TEST_CASE("radio verdicts: a frame the plausibility gate refuses is still a decode failure") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);

    const model::Ownship& own = rig.state().own;
    uint8_t frame[protocol::kAlptasFrameBytes];
    REQUIRE(protocol::alptas_encode(frame, neighbour(own), own.utc_s + 100, own.lat_1e7,
                                    own.lon_1e7) == Status::Ok);

    hear_alptas(rig, frame);
    settle(rig, t);

    CHECK(heard_verdict(rig) == radio::Verdict::Undecoded);
}

// OGN Diagnostics is payload type 66 of the same specification (F.2.1), not this device's fault.
TEST_CASE("radio verdicts: an ADS-L payload type we do not implement is a dialect, not a failure") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);
    const uint32_t bad_before = rig.state().air.rx_bad;

    protocol::AdslPacket p = adsl_from_neighbour(rig.state().own);
    p.Type = 66;
    hear_adsl(rig, p);
    settle(rig, t);

    CHECK(heard_verdict(rig) == radio::Verdict::Unsupported);
    CHECK(rig.state().air.rx_type == 1);
    CHECK(rig.state().air.rx_bad == bad_before);
}

TEST_CASE("radio verdicts: an ADS-L Traffic frame carrying no position is a decode failure") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);
    const uint32_t bad_before = rig.state().air.rx_bad;

    protocol::AdslPacket p = adsl_from_neighbour(rig.state().own);
    REQUIRE(p.has_alt());
    p.set_position_invalid();
    hear_adsl(rig, p);
    settle(rig, t);

    CHECK(heard_verdict(rig) == radio::Verdict::Undecoded);
    CHECK(rig.state().air.rx_type == 0);
    CHECK(rig.state().air.rx_bad == bad_before + 1);
}

TEST_CASE("radio verdicts: the frame we can read is read, and names its aircraft") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);

    const model::Ownship& own = rig.state().own;
    uint8_t frame[protocol::kAlptasFrameBytes];
    REQUIRE(protocol::alptas_encode(frame, neighbour(own), own.utc_s, own.lat_1e7, own.lon_1e7) ==
            Status::Ok);

    hear_alptas(rig, frame);
    settle(rig, t);

    CHECK(heard_verdict(rig) == radio::Verdict::Received);
}

TEST_CASE("radio verdicts: a registration frame names its sender and is not a target") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);
    const uint32_t tracked = static_cast<uint32_t>(rig.state().traffic.count());

    hear_callsign(rig, 0xC5D804, 58, "D-KXYZ");
    settle(rig, t);

    CHECK(heard_verdict(rig) == radio::Verdict::Named);
    CHECK(rig.state().air.rx_named == 1);
    CHECK(rig.state().air.rx_type == 0);
    CHECK(rig.state().air.rx_ok == 0);
    // A name is not a position, so nothing new is on the radar.
    CHECK(static_cast<uint32_t>(rig.state().traffic.count()) == tracked);
    REQUIRE(rig.state().callsigns.find(58, 0xC5D804) != nullptr);
    CHECK(std::string(rig.state().callsigns.find(58, 0xC5D804)) == "D-KXYZ");

    const radio::Entry* entry = heard_entry(rig);
    REQUIRE(entry != nullptr);
    CHECK(entry->address == 0xC5D804u);
    CHECK(entry->address_valid);
}

// A skyBlip on the apron beside a chatty neighbour used to report a climbing bad-frame count.
TEST_CASE("radio counters: a burst nothing was attempted on leaves rx_bad where it was") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    settle(rig, t);
    REQUIRE_FALSE(rig.state().own.fix_valid);

    model::AircraftObservation observation = neighbour(rig.state().own);
    observation.lat_1e7 = 481234567;
    observation.lon_1e7 = 87654321;
    uint8_t frame[protocol::kAlptasFrameBytes];
    REQUIRE(protocol::alptas_encode(frame, observation, Rig::kUtcBase, 481000000, 87000000) ==
            Status::Ok);

    hear_alptas(rig, frame);
    settle(rig, t);

    CHECK(rig.state().air.rx_wait == 1);
    CHECK(rig.state().air.rx_bad == 0);
    CHECK(rig.state().air.rx_ok == 0);
}

TEST_CASE("radio counters: a dialect we do not read is counted apart from a frame we refused") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);
    const uint32_t bad_before = rig.state().air.rx_bad;

    const model::Ownship& own = rig.state().own;
    uint8_t v6[protocol::kAlptasFrameBytes];
    REQUIRE(protocol::alptas_encode(v6, neighbour(own), own.utc_s, own.lat_1e7, own.lon_1e7) ==
            Status::Ok);
    v6[3] = static_cast<uint8_t>(v6[3] & 0xF0);
    protocol::alptas_set_crc(v6);

    hear_alptas(rig, v6);
    settle(rig, t);

    CHECK(rig.state().air.rx_type == 1);
    CHECK(rig.state().air.rx_wait == 0);
    CHECK(rig.state().air.rx_bad == bad_before);

    uint8_t stale[protocol::kAlptasFrameBytes];
    REQUIRE(protocol::alptas_encode(stale, neighbour(own), own.utc_s + 100, own.lat_1e7,
                                    own.lon_1e7) == Status::Ok);

    hear_alptas(rig, stale);
    settle(rig, t);

    CHECK(rig.state().air.rx_bad == bad_before + 1);
    CHECK(rig.state().air.rx_type == 1);
}

// skyblip#61: a dwell of ours that ended with the burst still in the chip is not a reception.
TEST_CASE("radio counters: a burst of ours that never completed is not a bad reception") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 2);
    const uint32_t bad_before = rig.state().air.rx_bad;

    events::RfEvent event{};
    event.type = events::RfEventType::Missed;
    event.band = model::Band::M;
    rig.product.bus().rf.push(event);
    settle(rig, t);

    CHECK(rig.state().air.tx_lost == 1);
    CHECK(rig.state().air.rx_bad == bad_before);
    CHECK(rig.state().radio_log.newest(0).verdict == radio::Verdict::Lost);
}

// 2026-09-19 on the bench: an hour of DEC beside a transmitting SoftRF, both holding a fix.
TEST_CASE("radio verdicts: a frame keyed on another second says whose second it wanted") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);
    const uint32_t bad_before = rig.state().air.rx_bad;

    const model::Ownship& own = rig.state().own;
    uint8_t frame[protocol::kAlptasFrameBytes];
    REQUIRE(protocol::alptas_encode(frame, neighbour(own), own.utc_s + 18, own.lat_1e7,
                                    own.lon_1e7) == Status::Ok);

    hear_alptas(rig, frame);
    settle(rig, t);

    const radio::Entry* entry = heard_entry(rig);
    REQUIRE(entry != nullptr);
    CHECK(entry->verdict == radio::Verdict::Miskeyed);
    CHECK(int(entry->key_offset_s) == 17);  // keyed at utc_s+18, heard in the second after utc_s
    CHECK(rig.state().air.rx_miskeyed == 1);
    CHECK(rig.state().air.rx_bad == bad_before);
    CHECK(rig.state().traffic.count() == 0);
}

// The address is in clear and the CRC covers it: a refused frame can still be attributed.
TEST_CASE("radio verdicts: a frame we refuse past its CRC still names the aircraft that sent it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);

    const model::Ownship& own = rig.state().own;
    uint8_t frame[protocol::kAlptasFrameBytes];
    REQUIRE(protocol::alptas_encode(frame, neighbour(own), own.utc_s + 18, own.lat_1e7,
                                    own.lon_1e7) == Status::Ok);

    hear_alptas(rig, frame);
    settle(rig, t);

    const radio::Entry* entry = heard_entry(rig);
    REQUIRE(entry != nullptr);
    CHECK(entry->address_valid);
    CHECK(entry->address == 0xC5D804u);
    CHECK(entry->source == model::Source::Alptas);
}

// A burst nothing framed used to climb the same counter a frame we got wrong does.
TEST_CASE("radio verdicts: a burst that names neither system is not a decode failure") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);
    const uint32_t bad_before = rig.state().air.rx_bad;

    const model::Ownship& own = rig.state().own;
    uint8_t frame[protocol::kAlptasFrameBytes];
    REQUIRE(protocol::alptas_encode(frame, neighbour(own), own.utc_s, own.lat_1e7, own.lon_1e7) ==
            Status::Ok);
    // Shares the chips the detector matched on and nothing after them.
    hear_mband(rig, 0xF5F3656Cu, frame, protocol::kAlptasFrameBytes);
    settle(rig, t);

    const radio::Entry* entry = heard_entry(rig);
    REQUIRE(entry != nullptr);
    CHECK(entry->verdict == radio::Verdict::Unframed);
    CHECK_FALSE(entry->address_valid);
    CHECK(rig.state().air.rx_unframed == 1);
    CHECK(rig.state().air.rx_bad == bad_before);
}

// A dead chip pair is an erasure with a known position, and the CRC says which flip it was.
TEST_CASE("radio verdicts: a burst the air damaged is corrected, not counted as broken") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);
    const uint32_t bad_before = rig.state().air.rx_bad;

    const model::Ownship& own = rig.state().own;
    uint8_t frame[protocol::kAlptasFrameBytes];
    REQUIRE(protocol::alptas_encode(frame, neighbour(own), own.utc_s, own.lat_1e7, own.lon_1e7) ==
            Status::Ok);

    hear_alptas(rig, frame, 18);
    settle(rig, t);

    CHECK(heard_verdict(rig) == radio::Verdict::Received);
    CHECK(rig.state().air.rx_bad == bad_before);
    CHECK(rig.state().traffic.count() == 1);
}
