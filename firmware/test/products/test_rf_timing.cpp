// What is on the 868 MHz air, and when. Every burst in these tests is a real
// scrambled ADS-L frame put on the channel at an absolute instant. The radio
// hears it only if the firmware had the receiver tuned there at that moment.
// A slot-map regression shows up as DEAF records, not as a missing feature.
#include <cstring>
#include <string>

#include "core/events/rf.h"
#include "core/gnss/first_fix.h"
#include "core/model/aircraft.h"
#include "core/model/band.h"
#include "core/protocol/air.h"
#include "core/radio/log.h"
#include "core/timing/slot.h"
#include "core/timing/timing_stats.h"
#include "core/timing/transmit.h"
#include "core/units/units.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "products/skyblip_go/settings.h"
#include "simulator/simulator.h"
#include "test/support/rf_channel.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {

// A receiver-only world: PPS unlocked keeps own-ship off air, so the only bursts are a neighbour's.
void listen_at(simulator::Simulator& h, int phase_ms, int slot) {
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_pps_locked(false);
    h.world().add_aircraft(800, 0, 0, 30, 180, phase_ms, slot);
}

// A device on the next bench: the real driver over its own part, armed for the M-band dwell.
struct Peer {
    models::Sx1262 chip;
    parts::Sx1262 radio{chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin};

    Peer() {
        REQUIRE(radio.begin() == Status::Ok);
        parts::RadioConfig cfg{};
        cfg.sync = protocol::kSharedSync;
        cfg.sync_bits = protocol::kSharedSyncBits;
        cfg.payload_bytes = protocol::kRxChipBytes;
        cfg.bitrate = protocol::kMbandChipRateBps;
        cfg.fdev_hz = protocol::kMbandDeviationHz;
        cfg.bandwidth_hz = protocol::kMbandChannelBandwidthHz;
        REQUIRE(radio.configure_radio(cfg) == Status::Ok);
        REQUIRE(radio.start_receive() == Status::Ok);
    }

    bool frames(const simulator::AirRecord& burst, protocol::Frame& out) {
        if (!chip.receive_air(burst.chips, burst.len, /*crc_error=*/false, burst.rssi_dbm,
                              burst.bitrate))
            return false;
        uint8_t buf[events::kRfEventBytes];
        const parts::RadioEvent ev = radio.poll(buf, sizeof(buf));
        REQUIRE(radio.start_receive() == Status::Ok);
        if (ev.type != parts::RadioEventType::RxDone) return false;
        return protocol::receive_mband(buf, ev.len, out);
    }
};

}  // namespace

// A quiet band still frames a few bursts a minute, and the tape is sixteen rows.
TEST_CASE("rf: a window the band's own noise walked through is counted, never put on the tape") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_pps_locked(false);
    const uint32_t start = past_settling(h);
    run_on(h, start, 1000);

    uint8_t burst[protocol::kSyncWindowChipBytes + protocol::kRxChipBytes];
    std::memcpy(burst, protocol::kSharedSync, protocol::kSyncWindowChipBytes);
    models::RfChannel noise(0xC0FFEE);
    for (size_t i = protocol::kSyncWindowChipBytes; i < sizeof(burst); i++)
        burst[i] = static_cast<uint8_t>(noise.next());

    const int rows_before = h.product().state().radio_log.count();
    const uint32_t bad_before = h.product().state().air.rx_bad;
    const uint32_t at_ms = start + 2000 + timing::kSlot0Start + 200;
    h.step(at_ms);
    REQUIRE(h.platform().chips().radio.receive_air(burst, sizeof(burst), /*crc_error=*/false,
                                                   /*rssi=*/-112, protocol::kMbandChipRateBps));
    run_on(h, at_ms, 200);

    CHECK(h.product().state().air.rx_noise == 1);
    CHECK(h.product().state().air.rx_bad == bad_before);
    CHECK(h.product().state().radio_log.count() == rows_before);
}

// The page a bench reads instead of guessing from two counters that only ever climb.
TEST_CASE("rf: what happened on air reaches the station log, sent and heard alike") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    h.world().add_aircraft(1200, 300, 50, 30, 90, 600, 0);
    run_on(h, past_settling(h), 6000);

    const radio::Log& log = h.product().state().radio_log;
    REQUIRE(log.count() > 0);
    bool sent = false, heard = false;
    for (int i = 0; i < log.count(); i++) {
        const radio::Entry& entry = log.newest(i);
        if (entry.event == radio::Event::Transmitted) sent = true;
        if (entry.event != radio::Event::Received) continue;
        heard = true;
        CHECK(entry.addr != 0);
        CHECK(entry.source == model::Source::AdslDirect);
        CHECK(entry.rssi_valid);
        CHECK(entry.utc);
    }
    CHECK(sent);
    CHECK(heard);
}

// One burst sent on one device and heard on another is compared on this phase alone.
TEST_CASE("rf: the log dates a burst to the millisecond of the second it landed in") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    h.world().add_aircraft(1200, 300, 50, 30, 90, 600, 0);
    run_on(h, past_settling(h), 6000);

    const radio::Log& log = h.product().state().radio_log;
    int dated = 0, channelled = 0;
    for (int i = 0; i < log.count(); i++) {
        const radio::Entry& entry = log.newest(i);
        REQUIRE(entry.phase_valid);
        CHECK(entry.into_ms < 1000);
        // The phase is where the burst ENDED, so only a dwell it cannot have run into names one.
        if (entry.band == model::Band::M && entry.into_ms < timing::kSlot0End - 100) {
            CHECK(entry.channel == 0);
            channelled++;
        }
        if (entry.event != radio::Event::Transmitted) continue;
        dated++;
        CHECK(entry.into_ms >= timing::kDirectStart);
        CHECK(entry.tx_span_valid);
        CHECK(entry.tx_span_us > 0);
    }
    CHECK(dated > 0);
    CHECK(channelled > 0);
}

// Two devices on a bench, both transmitting, neither ever hearing the other.
TEST_CASE("rf: a burst own-ship put on air is one another skyBlip frames") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    run_on(h, past_settling(h), 6000);

    const simulator::Air& air = h.world().air();
    Peer peer;
    int framed = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& mine = air.record(i);
        if (mine.event != simulator::AirEvent::Tx) continue;
        protocol::Frame heard{};
        REQUIRE(peer.frames(mine, heard));
        CHECK(heard.system == protocol::System::AdslDirect);
        protocol::AdslPacket p{};
        p.init();
        std::memcpy(&p.Version, heard.data, protocol::kAdslFrameBytes);
        REQUIRE(p.check_crc() == 0);
        p.descramble();
        CHECK(p.address() == h.platform().device_addr());
        framed++;
    }
    CHECK(framed > 0);
}

TEST_CASE("rf: a paraglider below the flight speed transmits every second, flight undefined") {
    for (const uint8_t type : {uint8_t{7}, uint8_t{1}}) {
        CAPTURE(int(type));
        simulator::Simulator h;
        REQUIRE(h.setup() == Status::Ok);
        h.product().settings().aircraft_type = type;
        h.world().set_fix(true);
        h.world().set_speed_kt(15);
        run_on(h, past_settling(h), 20000);

        const simulator::Air& air = h.world().air();
        Peer peer;
        int positions = 0;
        for (int i = 0; i < air.record_count(); i++) {
            const simulator::AirRecord& mine = air.record(i);
            if (mine.event != simulator::AirEvent::Tx) continue;
            protocol::Frame heard{};
            REQUIRE(peer.frames(mine, heard));
            protocol::AdslPacket p{};
            p.init();
            std::memcpy(&p.Version, heard.data, protocol::kAdslFrameBytes);
            p.descramble();
            if (!p.is_position()) continue;
            positions++;
            CHECK(p.FlightState == (type == 7 ? 0 : 1));
        }
        if (type == 7)
            CHECK(positions >= 19);
        else
            CHECK(positions <= 3);
    }
}

TEST_CASE("rf: a burst is heard only inside the dwell that owns its channel") {
    struct Case {
        int phase_ms;
        int slot;
        bool heard;
    };
    // The O-band dwell runs 205..395 (framed on our ground station's burst), the
    // M-band dwells 400..800 on 868.2 and 800..1000 on 868.4. 400..450 is the
    // slice we listen to but do not transmit in: FLARM-generation traffic lives
    // there.
    const Case cases[] = {
        {50, 0, false},
        {150, 0, false},
        {300, 0, false},
        {390, 0, false},
        {420, 0, true},
        {470, 0, true},
        {600, 0, true},
        {780, 0, true},
        {820, 1, true},
        {900, 1, true},
        {990, 1, true},
        // The second dwell reaches 1200 ms: its tail is heard on 868.4, not
        // deaf. Nothing of ours transmits there, everyone else still may.
        {100, 1, true},
        {190, 1, true},
        // Right time, wrong channel: a slot-1 burst still on 868.2 is a burst
        // we cannot hear, which is why the channel is part of the dwell.
        {900, 0, false},
        // Right channel, wrong time: 868.4 during slot 0.
        {600, 1, false},
    };
    for (const Case& c : cases) {
        simulator::Simulator h;
        listen_at(h, c.phase_ms, c.slot);
        h.run(4000);
        const int heard = count_of(h.world().air(), simulator::AirEvent::Rx);
        const int deaf = count_of(h.world().air(), simulator::AirEvent::Deaf);
        CAPTURE(c.phase_ms);
        CAPTURE(c.slot);
        CHECK((heard > 0) == c.heard);
        CHECK((deaf > 0) == !c.heard);
        CHECK((h.product().state().air.rx_ok > 0u) == c.heard);
    }
}

// A burst near the end of the second finishes after the second has rolled over.
// Its phase is its own instant's, not the instant we noticed it ended.
TEST_CASE("rf: a burst that straddles the second is dated by when it started") {
    simulator::Simulator h;
    listen_at(h, 995, 1);
    h.run(4000);
    const simulator::Air& air = h.world().air();
    REQUIRE(air.record_count() > 0);
    for (int i = 0; i < air.record_count(); i++) CHECK(air.record(i).phase_ms == 995);
}

TEST_CASE("rf: a heard burst is the frame that was on air, decoded by the real path") {
    simulator::Simulator h;
    listen_at(h, 600, 0);
    h.run(3000);
    REQUIRE(h.product().state().air.rx_ok > 0);
    REQUIRE(h.product().state().traffic.count() == 1);
    char line[160];
    REQUIRE(h.world().air().format(0, line, sizeof(line)) > 0);
    // The tape decodes what the receiver decoded: same address, CRC intact.
    CHECK(std::string(line).find("300001") != std::string::npos);
    CHECK(std::string(line).find("crc ok") != std::string::npos);
    CHECK(std::string(line).find("868.200") != std::string::npos);
}

// --- J. Transmit loopback ----------------------------------------------------
// SoftRF suppresses a transmission whose buffer equals the last frame received
// and reports "$PSRFE,RF loopback is detected on Tx" (src/driver/RF.cpp:381-396).
// That guard exists because it happened in the field, and the shape of its
// firmware is why: one RF driver owns a shared TxBuffer/RxBuffer pair, a received
// frame is parsed out of the same memory a transmission is composed into, and
// its relay and bridge paths do put received traffic back on air.
//
// Ours cannot reach that state, and this is the case that says so rather than a
// paragraph claiming it. The transmit buffer (RadioService::outgoing_) is only
// ever written by protocol::from_own, whose inputs are own-ship state, the device
// address and the settings; a received frame's only path is the RfEvent queue
// into TrafficService and the traffic table, which nothing transmits from. There
// is no relay feature, no repeater and no second writer. So there is no guard in
// the driver: a guard against an impossible fault is a test nobody can fail
// honestly and a comparison in the one place a dwell cannot afford one.
//
// What we do instead is assert the property the guard would protect, over the
// real air, with both directions live in the same second.
TEST_CASE("rf: nothing own-ship transmits is a frame own-ship received") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    // A neighbour transmitting inside the first M-band dwell, so every second
    // carries a reception and a transmission on the same radio.
    h.world().add_aircraft(1200, 300, 50, 30, 90, 600, 0);
    run_on(h, past_settling(h), 6000);

    const simulator::Air& air = h.world().air();
    int received = 0, transmitted = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& mine = air.record(i);
        if (mine.event != simulator::AirEvent::Tx) continue;
        transmitted++;
        protocol::Frame sent{};
        REQUIRE(simulator::Air::framed(mine, sent));
        protocol::AdslPacket p{};
        p.init();
        std::memcpy(&p.Version, sent.data, protocol::kAdslFrameBytes);
        REQUIRE(p.check_crc() == 0);
        p.descramble();
        // Every burst we put on air is ours, by address: a relayed frame would
        // carry the neighbour's.
        CHECK(p.address() == h.platform().device_addr());

        for (int j = 0; j < air.record_count(); j++) {
            const simulator::AirRecord& heard = air.record(j);
            if (heard.event != simulator::AirEvent::Rx) continue;
            const bool identical =
                heard.len == mine.len && std::memcmp(heard.chips, mine.chips, mine.len) == 0;
            CHECK_FALSE(identical);
        }
    }
    for (int i = 0; i < air.record_count(); i++)
        if (air.record(i).event == simulator::AirEvent::Rx) received++;
    // Neither half may be zero, or the case above proves nothing.
    CHECK(received > 0);
    CHECK(transmitted > 0);
    CHECK(h.product().state().air.rx_ok > 0);
    CHECK(h.product().state().air.tx_ok > 0);
}

// --- J. Range sanity, end to end --------------------------------------------
// The gate on the traffic table's door (core/traffic/sanity.h) with the whole
// path in front of it: a real frame, on the real air, through the model's sync
// detector, the driver, the executor and the decoder. A frame that survives all
// of that and still claims to be 120 km away did not arrive from there.
TEST_CASE("rf: a decoded burst claiming an impossible range never reaches the radar") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_pps_locked(false);  // receive only, so the tape is the neighbour's
    h.world().add_aircraft(120000, 0, 0, 30, 180, 600, 0);
    h.run(4000);

    // Heard, decoded, CRC intact: the frame is not being refused by the radio or
    // by the protocol layer.
    CHECK(h.product().state().air.rx_ok > 0);
    CHECK(h.product().state().air.rx_bad == 0);
    // And refused by the table, counted, with nothing on the screen.
    CHECK(h.product().state().traffic.count() == 0);
    CHECK(h.product().state().traffic.implausible_count() > 0);
    CHECK(h.product().state().alarm_level == traffic::Level::None);
}

// The same air with the same aircraft at a range this radio can actually reach:
// the gate is a ceiling on nonsense, not a filter on traffic.
TEST_CASE("rf: a burst from a range the link budget allows is traffic as before") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_pps_locked(false);
    h.world().add_aircraft(8000, 0, 0, 30, 180, 600, 0);
    h.run(4000);

    CHECK(h.product().state().air.rx_ok > 0);
    CHECK(h.product().state().traffic.count() == 1);
    CHECK(h.product().state().traffic.implausible_count() == 0);
}
