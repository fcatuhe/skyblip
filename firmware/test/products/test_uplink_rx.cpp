// What a skyPost ground station actually gets onto a skyBlip Go's screen.
//
// The tree had an ADS-L uplink codec, a Reed-Solomon suite around it and a
// radio service that armed the O-band dwell for its sync word - and no product
// caller at all. Every RxDone went to protocol::receive_mband, which frames the
// two M-band systems and nothing else, so an uplink frame that arrived was
// counted as rx_bad and thrown away. Feature::UplinkRx had no reader, so the
// capability the product claimed - ADS-L uplink receive - was not one it had.
//
// So nothing here pushes an RfEvent. A real encoded frame goes on the virtual
// air, through the SX1262 model's own sync detector, into the dwell the product
// armed for itself. If the arming and the transmitter disagree about the sync
// word, the length, or the chip rate, the model delivers nothing and these
// cases go red - which is exactly how the second bug on this branch was found:
// the dwell was armed for a length byte of 0x18, the M band's 24-byte ADS-L
// frame, behind an O-band sync word whose frame is 255 bytes.
#include <initializer_list>

#include "core/events/rf.h"
#include "core/model/aircraft.h"
#include "core/model/band.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "core/settings/address.h"
#include "core/timing/slot.h"
#include "core/traffic/lease.h"
#include "core/units/units.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/platform/host/clock.h"
#include "hardware/platform/host/identity.h"
#include "ports/null.h"
#include "test/support/product_rig.h"
#include "test/support/rf_channel.h"

using namespace skyblip;

namespace {

// An aircraft as a ground station knows it: an address, a place, a height. That
// is all one uplink record carries (16 bytes of it), and it is deliberately less
// than a direct ADS-L frame gives.
model::AircraftObservation relayed_aircraft(Rig& rig, uint32_t address, int32_t north_m,
                                            int32_t east_m, int32_t up_m) {
    const model::Ownship& own = rig.state().own;
    model::AircraftObservation observation{};
    observation.address = address;
    observation.address_mapping_table = 6;
    observation.aircraft_category = 4;
    observation.flight_state = 2;
    observation.speed_q = 140;
    observation.speed_valid = true;
    observation.latitude_1e7 =
        own.latitude_1e7 + static_cast<int32_t>(static_cast<int64_t>(north_m) * 1000000 / 11132);
    observation.longitude_1e7 =
        own.longitude_1e7 + static_cast<int32_t>(static_cast<int64_t>(east_m) * 1000000 / 7460);
    observation.altitude_m = to_metres(Millimetres(own.altitude_hae_mm)).v + up_m;
    observation.position_valid = true;
    return observation;
}

// One skyPost transmission: the frame the ground station composes, put on air in
// the uplink slot (§C.5, 200..450 ms) at §C.4's 200 kbps. The rig is left at the
// top of the next second, the way rig.second() leaves it.
//
// Returns what the radio model made of the burst: false is a deaf dwell, and it
// is a result worth asserting on rather than a test that quietly passes.
bool relay(Rig& rig, uint32_t& t, const model::AircraftObservation* aircraft, int n,
           int corrupt_bytes = 0) {
    rig.push_timed_fix(25000, 900);

    protocol::AdslUplink codec;
    uint8_t frame[protocol::kUplinkFrameBytes] = {0};
    REQUIRE(codec.encode(aircraft, n, /*key_index=*/0, frame) == Status::Ok);
    if (corrupt_bytes > 0) {
        RfChannel channel(0x5150);
        channel.apply_symbol_errors(frame, sizeof(frame), corrupt_bytes);
    }

    uint8_t burst[protocol::kUplinkBurstBytes] = {0};
    const size_t burst_length = protocol::encode_oband(frame, burst);

    // Into the dwell, not around it: the radio has to have opened the O-band
    // window (205 ms, one jitter guard before the ground station starts) before
    // a burst arriving there means anything at all.
    const uint32_t at_ms = t + timing::kGroundEmitStartMs;
    rig.run(t, t + 200);
    rig.platform.clock().set_millis(at_ms);
    rig.product.run_pass(at_ms);
    const bool heard = rig.platform.chips().radio.receive_air(
        burst, static_cast<uint16_t>(burst_length), /*crc_error=*/false, /*rssi=*/-92,
        protocol::kUplinkChipRateBps);

    rig.run(at_ms + 10, t + 950);
    t += 1000;
    rig.utc_offset_s++;
    return heard;
}

// A neighbour we hear for ourselves, on the M band, in the direct slot. Same
// address as a relayed one is the interesting case.
void hear_directly(Rig& rig, uint32_t& t, uint32_t address, int32_t north_m, int32_t east_m,
                   int32_t up_m) {
    rig.push_timed_fix(25000, 900);
    const model::Ownship& own = rig.state().own;

    model::Ownship transmitter = own;
    transmitter.latitude_1e7 +=
        static_cast<int32_t>(static_cast<int64_t>(north_m) * 1000000 / 11132);
    transmitter.longitude_1e7 +=
        static_cast<int32_t>(static_cast<int64_t>(east_m) * 1000000 / 7460);
    transmitter.altitude_hae_mm += up_m * 1000;
    transmitter.speed_mm_s = 40000;
    transmitter.track_cdeg = 18000;

    protocol::AdslPacket packet;
    protocol::from_own(packet, transmitter, address, /*address_mapping_table=*/6,
                       /*aircraft_category=*/4);
    packet.scramble();
    packet.set_crc();

    uint8_t chips[protocol::kTxChipBytes] = {0};
    const size_t chip_length = protocol::encode_mband(protocol::kAdslSyncWord, packet.Data,
                                                      protocol::kAdslFrameBytes, chips);

    // 500 ms is inside the direct slot and inside the first M-band dwell.
    rig.run(t, t + 500);
    rig.platform.clock().set_millis(t + 500);
    rig.platform.chips().radio.receive_air(chips, static_cast<uint16_t>(chip_length),
                                           /*crc_error=*/false, /*rssi=*/-70,
                                           protocol::kMbandChipRateBps);
    rig.run(t + 500, t + 950);
    t += 1000;
    rig.utc_offset_s++;
}

const traffic::Target* target_for(Rig& rig, uint32_t address) {
    const traffic::TrafficTable& table = rig.state().traffic;
    const int index = table.find(/*address_mapping_table=*/6, address);
    return index < 0 ? nullptr : table.at(index);
}

int sourced(Rig& rig, model::Source source) {
    const traffic::TrafficTable& table = rig.state().traffic;
    int n = 0;
    for (int i = 0; i < traffic::TrafficTable::kCapacity; i++) {
        const traffic::Target* t = table.at(i);
        if (t != nullptr && t->used && t->observation.source == source) n++;
    }
    return n;
}

void fly(Rig& rig, uint32_t& t, uint32_t seconds) { rig.seconds(t, seconds, 25000, 900); }

// The service alone, so that the only thing a case changes is what the product
// claims to do.
struct FeatureRig {
    platform::host::Clock clock;
    platform::host::Identity identity{platform::host::Identity::kSerial};
    ports::NullRoles null;
    ports::Roles roles{
        clock,          null.rf,          null.link, null.display,         null.key_value,
        null.log_flash, null.annunciator, null.dfu,  null.die_temperature, null.indicator,
        null.gnss,      identity};
    bus::Bus bus{};
    bus::State state{};
    diagnostics::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    go::TrafficService traffic;

    explicit FeatureRig(go::Feature declared) : traffic(context, declared) {
        roles.capabilities = ports::Capability::Rf;
        state.own.fix_valid = true;
        state.own.utc_valid = true;
        state.own.utc_s = Rig::kBaseUtcS;
        // The same place Rig flies, because the table's range gate
        // (core/traffic/sanity.h) measures a relayed position against own-ship's:
        // a rig claiming a fix at 0N 0E is 5000 km from the aircraft this frame
        // relays, and every one of them is then correctly refused as a ghost.
        state.own.latitude_1e7 = 485000000;
        state.own.longitude_1e7 = 85000000;
        REQUIRE(traffic.setup() == Status::Ok);
    }

    // Straight onto the bus, because what is under test here is the service and
    // not the dwell: the band the executor stamped is what the service reads.
    void deliver(const uint8_t* frame) {
        events::RfEvent event{};
        event.type = events::RfEventType::RxDone;
        event.band = model::Band::O;
        event.bytes = protocol::kUplinkFrameBytes;
        event.rssi_dbm = -92;
        for (int i = 0; i < protocol::kUplinkFrameBytes; i++) event.data[i] = frame[i];
        bus.rf.push(event);
        traffic.tick(4000);
    }
};

}  // namespace

// THE GUARD. Delete the uplink branch from TrafficService, or the O-band arming
// from RadioService, and this is the case that goes red.
TEST_CASE("uplink: one ground-station frame carries several aircraft onto the radar") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);

    model::AircraftObservation relayed[3] = {
        relayed_aircraft(rig, 0x4A0001, 2400, -600, 120),
        relayed_aircraft(rig, 0x4A0002, -1800, 900, -250),
        relayed_aircraft(rig, 0x4A0003, 400, 3100, 60),
    };
    REQUIRE(relay(rig, t, relayed, 3));

    CHECK(rig.state().air.uplink_frames == 1);
    CHECK(rig.state().air.uplink_bad == 0);
    CHECK(rig.state().air.uplink_targets == 3);
    CHECK(rig.state().traffic.count() == 3);
    CHECK(sourced(rig, model::Source::AdslUplink) == 3);

    // Not merely three blips: the positions the ground station sent are the
    // positions the table holds, to the metre the record's resolution allows.
    for (const model::AircraftObservation& sent : relayed) {
        const traffic::Target* got = target_for(rig, sent.address);
        REQUIRE(got != nullptr);
        CHECK(got->observation.latitude_1e7 == sent.latitude_1e7);
        CHECK(got->observation.longitude_1e7 == sent.longitude_1e7);
        CHECK(got->observation.altitude_m == sent.altitude_m);
        CHECK(got->observation.position_valid);
        CHECK(got->observation.source == model::Source::AdslUplink);
        CHECK(got->observation.received.at_s == rig.state().own.utc_s);
    }
}

// The counters the bug hid behind. An uplink frame is not an M-band framing
// failure, and a Reed-Solomon refusal is not one either: both used to land in
// rx_bad, where a feature that did not exist looked exactly like a noisy site.
TEST_CASE("uplink: a frame Reed-Solomon cannot repair is counted apart and dropped") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);
    const uint32_t rx_bad_before = rig.state().air.rx_bad;

    model::AircraftObservation relayed[2] = {
        relayed_aircraft(rig, 0x4B0001, 1500, 200, 0),
        relayed_aircraft(rig, 0x4B0002, -900, -400, 80),
    };
    // Forty symbol errors: RS(255,223) corrects sixteen, so this is past the
    // point where the decoder is allowed to guess.
    REQUIRE(relay(rig, t, relayed, 2, /*corrupt_bytes=*/40));

    CHECK(rig.state().air.uplink_frames == 1);
    CHECK(rig.state().air.uplink_bad == 1);
    CHECK(rig.state().air.uplink_targets == 0);
    CHECK(rig.state().traffic.count() == 0);
    CHECK(rig.state().air.rx_bad == rx_bad_before);
    CHECK(rig.state().air.rx_ok == 0);
}

// A relay is a rebroadcast of something we may already be hearing for ourselves.
// One aircraft, two paths, one target - and the better report is the one that
// stays, even though the relay of it arrives afterwards and is therefore newer.
TEST_CASE("uplink: an aircraft heard directly and relayed is one target, not two") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);

    hear_directly(rig, t, 0x4C0001, 1200, 300, 40);
    REQUIRE(rig.state().traffic.count() == 1);
    const traffic::Target* direct = target_for(rig, 0x4C0001);
    REQUIRE(direct != nullptr);
    const int32_t direct_latitude = direct->observation.latitude_1e7;

    // The ground station heard the same aircraft and puts it on the uplink a
    // second later, with a position 300 m behind where we already have it.
    model::AircraftObservation stale = relayed_aircraft(rig, 0x4C0001, 900, 300, 40);
    REQUIRE(relay(rig, t, &stale, 1));

    CHECK(rig.state().traffic.count() == 1);
    CHECK(rig.state().air.uplink_frames == 1);
    CHECK(rig.state().air.uplink_bad == 0);
    // Counted as decoded, refused as an update: the frame was good, the target
    // was better.
    CHECK(rig.state().air.uplink_targets == 1);

    const traffic::Target* merged = target_for(rig, 0x4C0001);
    REQUIRE(merged != nullptr);
    CHECK(merged->observation.source == model::Source::AdslDirect);
    CHECK(merged->observation.latitude_1e7 == direct_latitude);
    CHECK(merged->observation.altitude_valid);
    CHECK(merged->observation.altitude_m == stale.altitude_m);
}

// And the other side of that rule, so the hold is a hold and not a block: an
// aircraft that goes quiet on the M band - behind a ridge, or out of our range
// but not the ground station's - is picked up by the relay once the direct
// report has gone as stale as the alarm layer's own patience with it.
TEST_CASE("uplink: the relay takes over once the direct report has gone stale") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);

    hear_directly(rig, t, 0x4C0002, 1200, 300, 40);
    REQUIRE(rig.state().traffic.count() == 1);
    const traffic::Target* direct = target_for(rig, 0x4C0002);
    REQUIRE(direct != nullptr);

    fly(rig, t, traffic::direct_preferred_max_age_s(direct->observation) + 1);

    model::AircraftObservation later = relayed_aircraft(rig, 0x4C0002, 500, 300, 40);
    REQUIRE(relay(rig, t, &later, 1));

    CHECK(rig.state().traffic.count() == 1);
    const traffic::Target* merged = target_for(rig, 0x4C0002);
    REQUIRE(merged != nullptr);
    CHECK(merged->observation.source == model::Source::AdslUplink);
    CHECK(merged->observation.latitude_1e7 == later.latitude_1e7);
}

// The phantom a relay can make that a direct reception never can: a ground
// station hears us and puts us in the frame it sends back. Own-ship on the
// radar, at own-ship's position, is a permanent level-3 collision with itself.
TEST_CASE("uplink: own-ship relayed back by the ground station is not traffic") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);

    const uint32_t own_address = *rig.state().activation.address();
    model::AircraftObservation echo[2] = {
        relayed_aircraft(rig, own_address, 0, 0, 0),
        relayed_aircraft(rig, 0x4D0001, 2000, 0, 0),
    };
    echo[0].address_mapping_table = settings::kAddressMappingTable;
    REQUIRE(relay(rig, t, echo, 2));

    CHECK(rig.state().air.uplink_frames == 1);
    CHECK(rig.state().traffic.count() == 1);
    CHECK(rig.state().air.uplink_targets == 1);
    CHECK(target_for(rig, own_address) == nullptr);
    CHECK(target_for(rig, 0x4D0001) != nullptr);
}

// The feature is what turns the path on, following the pattern the companion
// link established: a claim in products/skyblip_go/features.h with a reader.
// Take it away and the frame still arrives at the service and is still not
// decoded.
TEST_CASE("uplink: with Feature::UplinkRx off, a ground frame decodes to nothing") {
    model::AircraftObservation relayed{};
    relayed.address = 0x4E0001;
    relayed.address_mapping_table = 6;
    relayed.latitude_1e7 = 485000000;
    relayed.longitude_1e7 = 85000000;
    relayed.altitude_m = 1000;
    relayed.position_valid = true;

    protocol::AdslUplink codec;
    uint8_t frame[protocol::kUplinkFrameBytes] = {0};
    REQUIRE(codec.encode(&relayed, 1, 0, frame) == Status::Ok);

    FeatureRig silent(go::Feature::None);
    CHECK_FALSE(silent.traffic.uplink_enabled());
    silent.deliver(frame);
    CHECK(silent.state.air.uplink_frames == 0);
    CHECK(silent.state.air.uplink_targets == 0);
    CHECK(silent.state.traffic.count() == 0);
    // And not counted as an M-band failure either: the product simply does not
    // claim this band.
    CHECK(silent.state.air.rx_bad == 0);

    // The same rig, the same frame: the feature is the difference, which is what
    // makes it a gate and not decoration.
    FeatureRig listening(go::Feature::UplinkRx);
    CHECK(listening.traffic.uplink_enabled());
    listening.deliver(frame);
    CHECK(listening.state.air.uplink_frames == 1);
    CHECK(listening.state.air.uplink_targets == 1);
    CHECK(listening.state.traffic.count() == 1);
}

// Point 6 of the brief, as a case rather than a paragraph: what the O-band dwell
// is armed with, read off the chip the product programmed. The arming and a
// §C.4 transmitter have to agree on all three or the dwell is deaf, and two of
// the three were wrong.
TEST_CASE("uplink: the O-band dwell is armed for the modulation SRD-860 C.4 defines") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 2);

    const models::Sx1262& chip = rig.platform.chips().radio;

    // Inside the uplink dwell: §C.4's chip rate, its Gaussian filter, a receiver
    // wide enough for its 250 kHz channel, and 255 bytes to read.
    rig.push_timed_fix(25000, 900);
    rig.run(t, t + 200);
    rig.platform.clock().set_millis(t + timing::kGroundEmitStartMs);
    rig.product.run_pass(t + timing::kGroundEmitStartMs);
    // The chip reports what its PLL word resolves to, which is a hertz or two
    // off the channel it was asked for.
    CHECK(timing::kObandHz - chip.frequency_hz < 100);
    CHECK(chip.bitrate == protocol::kUplinkChipRateBps);
    CHECK(chip.pulse_shape == parts::sx::kGaussianBt0p5);
    CHECK(chip.rx_bandwidth == 0x19);
    CHECK(chip.payload_bytes == protocol::kUplinkFrameBytes);
    CHECK(chip.sync_bits == protocol::kUplinkSyncBits);
    CHECK(chip.sync[0] == 0x2D);
    CHECK(chip.sync[1] == 0xD4);
    // §D.1.1's Packet Length field: the message behind it, excluding the length
    // byte, which for this frame is the whole RS(255,223) codeword. It used to
    // be 0x18 - 24 - which is the M band's ADS-L data length and no uplink
    // frame's, so the detector never matched and the dwell heard nothing at all.
    CHECK(chip.sync[2] == protocol::kUplinkFrameBytes);

    // And back on the M band, §C.2's, which is not the same modulation.
    rig.run(t + timing::kGroundEmitStartMs + 10, t + 500);
    CHECK(chip.bitrate == protocol::kMbandChipRateBps);
    CHECK(chip.pulse_shape == parts::sx::kGaussianBt0p5);
    CHECK(chip.payload_bytes == protocol::kRxChipBytes);
    t += 1000;
    rig.utc_offset_s++;
}

// The regression for the modulation half of that: a §C.4 burst into a dwell
// still framing §C.2's chip rate is deaf, and the model says so rather than
// delivering it anyway.
TEST_CASE("uplink: a 200 kbps burst is not heard by a dwell framing 100 kbps") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);

    model::AircraftObservation relayed = relayed_aircraft(rig, 0x4F0001, 1000, 0, 0);
    protocol::AdslUplink codec;
    uint8_t frame[protocol::kUplinkFrameBytes] = {0};
    REQUIRE(codec.encode(&relayed, 1, 0, frame) == Status::Ok);
    uint8_t burst[protocol::kUplinkBurstBytes] = {0};
    const size_t burst_length = protocol::encode_oband(frame, burst);

    // In the direct slot, where the radio is on the M band at 100 kbps.
    rig.push_timed_fix(25000, 900);
    rig.run(t, t + 500);
    CHECK_FALSE(rig.platform.chips().radio.receive_air(burst, static_cast<uint16_t>(burst_length),
                                                       false, -92, protocol::kUplinkChipRateBps));
    rig.run(t + 500, t + 950);
    CHECK(rig.state().air.uplink_frames == 0);
}
