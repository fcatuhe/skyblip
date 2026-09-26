// One radio, one second, two bands: the dwell map decides what the receiver can
// hear and when it is allowed to speak. These pin down both halves. A dwell that
// runs past its edge or a burst that starts too late to finish inside the direct
// slot transmits into someone else's window, and a device that keeps transmitting
// once UTC is gone does it blind. Without a clock the answer is listen only.
#include <initializer_list>

#include "core/model/ownship.h"
#include "core/timing/channel.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"

using namespace skyblip::timing;

static ClockState anchored() { return ClockState{true, true, 0}; }

// Our cut of the second: two 400 ms M-band dwells, one channel each, so slot 1
// runs 800..1200 and its tail is the head of the next second. The uplink dwell
// is framed on the window our own ground station transmits in (210..390 ms, a
// burst that completes by 390). The M-band dwell opens at 400 to catch
// FLARM-generation traffic that starts around 405, earlier than the ADS-L direct
// slot our own bursts respect.
TEST_CASE("timing: dwell map matches the decided band split") {
    CHECK(Scheduler::state_at(0) == SlotState::Slot1);
    CHECK(Scheduler::state_at(199) == SlotState::Slot1);
    CHECK(Scheduler::state_at(200) == SlotState::SwitchMtoO);
    CHECK(Scheduler::state_at(204) == SlotState::SwitchMtoO);
    CHECK(Scheduler::state_at(205) == SlotState::UplinkRxO);
    CHECK(Scheduler::state_at(390) == SlotState::UplinkRxO);
    CHECK(Scheduler::state_at(394) == SlotState::UplinkRxO);
    CHECK(Scheduler::state_at(395) == SlotState::SwitchOtoM);
    CHECK(Scheduler::state_at(399) == SlotState::SwitchOtoM);
    CHECK(Scheduler::state_at(400) == SlotState::Slot0);
    CHECK(Scheduler::state_at(798) == SlotState::Slot0);
    CHECK(Scheduler::state_at(799) == SlotState::Hop);
    CHECK(Scheduler::state_at(800) == SlotState::Slot1);
    CHECK(Scheduler::state_at(999) == SlotState::Slot1);
}

TEST_CASE("timing: every band edge keeps its guard") {
    CHECK(kUplinkRxStart <= kGroundEmitStart - kJitterGuardMs);
    CHECK(kUplinkRxEnd >= kGroundEmitEnd + kJitterGuardMs);
    // M->O after slot 1's tail, then O->M before slot 0.
    CHECK(kUplinkRxStart - kSlot1Wrap == kJitterGuardMs);
    CHECK(kSlot0Start - kUplinkRxEnd == kJitterGuardMs);
    // Two equal M-band dwells, one channel each.
    CHECK(kSlot0End - kSlot0Start == kSlot1End - kSlot1Start);
}

TEST_CASE("timing: band and channel per dwell") {
    CHECK(Scheduler::band_at(300) == Band::O);
    // The tail of slot 1: still M-band, still the second channel.
    CHECK(Scheduler::band_at(100) == Band::M);
    CHECK(Scheduler::freq_at(100) == kMband1Hz);
    CHECK(Scheduler::slot_of(100) == 1);
    CHECK(Scheduler::band_at(420) == Band::M);
    CHECK(Scheduler::band_at(500) == Band::M);
    CHECK(Scheduler::band_at(900) == Band::M);
    // §C.2: two M-band channels, one per direct half, so a receiver visits both.
    CHECK(Scheduler::freq_at(300) == kObandHz);
    CHECK(Scheduler::freq_at(420) == kMband0Hz);
    CHECK(Scheduler::freq_at(500) == kMband0Hz);
    CHECK(Scheduler::freq_at(900) == kMband1Hz);
}

TEST_CASE("timing: uplink window and direct slot predicates") {
    CHECK(Scheduler::in_uplink_rx(205));
    CHECK(Scheduler::in_uplink_rx(394));
    CHECK_FALSE(Scheduler::in_uplink_rx(395));
    // The dwells are wider than the direct slot at both ends: we listen from
    // 400 and to 1200, we transmit only inside 450..1000.
    CHECK_FALSE(Scheduler::in_direct_slot(420));
    CHECK(Scheduler::in_direct_slot(450));
    CHECK(Scheduler::in_direct_slot(999));
    CHECK_FALSE(Scheduler::in_direct_slot(100));
    CHECK_FALSE(Scheduler::in_direct_slot(300));
    CHECK(Scheduler::slot_of(420) == 0);
    CHECK(Scheduler::slot_of(500) == 0);
    CHECK(Scheduler::slot_of(900) == 1);
    CHECK(Scheduler::slot_of(300) == -1);
}

// The executor cannot be handed a burst mid-dwell: a plan queues behind the one it is flying.
TEST_CASE("timing: the dwell owns its burst from the instant it opens") {
    Scheduler s;
    // Slot 0 opens at 400 and places its burst from 450, so 400 is where the plan must carry it.
    CHECK_FALSE(Scheduler::in_direct_slot(kSlot0Start));
    CHECK(Scheduler::in_own_tx_dwell(kSlot0Start));
    CHECK(s.plan(kSlot0Start, anchored()).tx_allowed);
    CHECK(Scheduler::in_own_tx_dwell(kSlot1Start));
    CHECK(s.plan(kSlot1Start, anchored()).tx_allowed);
    // Slot 1's tail is the same dwell, and it carries the callsign burst.
    CHECK(Scheduler::in_own_tx_dwell(0));
    CHECK(Scheduler::in_own_tx_dwell(kSlot1Wrap - 1));
    CHECK(s.plan(100, anchored()).tx_allowed);
    // The uplink dwell and both band edges own no direct-slot time at all.
    CHECK_FALSE(Scheduler::in_own_tx_dwell(202));
    CHECK_FALSE(Scheduler::in_own_tx_dwell(300));
    CHECK_FALSE(Scheduler::in_own_tx_dwell(397));
}

TEST_CASE("timing: a dwell stops early enough to retune before the next one") {
    Scheduler s;
    // The O->M edge is the safety-critical one: 5 ms of guard, then M-band at 400.
    CHECK(s.plan(300, anchored()).start_ms == kUplinkRxStart);
    CHECK(s.plan(300, anchored()).end_ms == kUplinkRxEnd);
    // The tail is the same dwell as 900 ms: one plan, 800..1200.
    CHECK(s.plan(100, anchored()).start_ms == kSlot1Start);
    CHECK(s.plan(100, anchored()).end_ms == kSlot1End);
    CHECK(s.plan(500, anchored()).start_ms == kSlot0Start);
    CHECK(s.plan(500, anchored()).end_ms == kSlot0End - kHopGuardMs);
    CHECK(s.plan(900, anchored()).end_ms == kSlot1End);
    CHECK(s.plan(900, anchored()).start_ms == kSlot1Start);
}

TEST_CASE("timing: TX only in M-band direct slots when clock is anchored") {
    Scheduler s;
    SlotPlan p = s.plan(500, anchored());
    CHECK(p.tx_allowed);
    CHECK(p.band == Band::M);
    CHECK_FALSE(p.listen_only);
    // The uplink window belongs to the ground infrastructure.
    p = s.plan(300, anchored());
    CHECK_FALSE(p.tx_allowed);
    CHECK(p.band == Band::O);
}

TEST_CASE("timing: PPS lost within holdover, still receiving, no slotted TX") {
    Scheduler s;
    ClockState c{true, false, 5000};
    SlotPlan p = s.plan(500, c);
    CHECK_FALSE(p.listen_only);
    CHECK_FALSE(p.tx_allowed);
}

// A burst drained between the edge and the sentence naming it was dated a second early.
TEST_CASE("timing: the dated second moves with the edge, not with the sentence") {
    ClockState clock{};
    clock.utc_valid = true;
    clock.pps_locked = true;
    clock.utc_s = 45296;
    clock.utc_edge_us = 12'000'000;
    clock.pps_edge_us = 12'000'000;

    carry_utc_to_edge(clock, 12'000'000);
    CHECK(clock.utc_s == 45296);

    carry_utc_to_edge(clock, 13'000'000);
    CHECK(clock.utc_s == 45297);
    CHECK(clock.pps_edge_us == 13'000'000);

    // A pass that saw no edge for three seconds catches up on the one it does see.
    carry_utc_to_edge(clock, 16'000'100);
    CHECK(clock.utc_s == 45300);
    CHECK(clock.utc_edge_us == 16'000'100);
}

TEST_CASE("timing: a clock no solution has dated yet carries no second to advance") {
    ClockState clock{};
    carry_utc_to_edge(clock, 13'000'000);
    CHECK(clock.utc_s == 0);
    CHECK(clock.pps_edge_us == 13'000'000);
}

TEST_CASE("timing: past holdover or no UTC, listen only, fail closed") {
    Scheduler s;
    ClockState past{true, false, kPpsHoldoverMs + 1};
    CHECK(s.plan(500, past).listen_only);
    CHECK_FALSE(s.plan(500, past).tx_allowed);

    ClockState no_utc{false, true, 0};
    CHECK(s.plan(500, no_utc).listen_only);
    CHECK_FALSE(s.plan(500, no_utc).tx_allowed);
}

namespace {

Transmitter airborne_transmitter(uint32_t addr = 0x5B7E57) {
    Transmitter t;
    t.configure(addr);
    return t;
}

// The plan the scheduler hands the radio service for a phase inside a slot.
SlotPlan slot_plan(int phase_ms) {
    Scheduler s;
    return s.plan(phase_ms, anchored());
}

int instant_at(uint32_t addr, uint32_t utc) {
    const int phase = Transmitter::slot_in(utc, true) == 0 ? 500 : 900;
    return airborne_transmitter(addr).attempt(slot_plan(phase), utc, utc * 1000, true, 0).at_ms;
}

bool position_burst(const Transmitter::Attempt& a) {
    return a.go && a.payload == Transmitter::Payload::Position;
}

bool bursts_overlap(int one_ms, int other_ms) {
    const int apart = one_ms > other_ms ? one_ms - other_ms : other_ms - one_ms;
    return apart < static_cast<int>(Transmitter::kAirTimeMs);
}

}  // namespace

TEST_CASE("transmit: the instant is inside the direct slot, with room for the burst") {
    Transmitter t = airborne_transmitter();
    int earliest = 1000, latest = 0;
    // Every other second is the lower channel's: §C.2.5's alternation is the clock's.
    for (uint32_t utc = 1000; utc < 6000; utc += 2) {
        const Transmitter::Attempt a = t.attempt(slot_plan(500), utc, utc * 1000, true, 0);
        REQUIRE(a.go);
        // Not from kSlot0Start: the dwell opens at 400, the direct slot at 450.
        CHECK(a.at_ms >= kDirectStart);
        // The burst also has to end before the channel hop at 800, not just
        // before the direct slot ends at 1000.
        CHECK(a.at_ms + static_cast<int>(Transmitter::kAirTimeMs) +
                  Transmitter::kCompletionSlackMs <=
              kSlot0End);
        CHECK(a.freq_hz == kMband0Hz);
        if (a.at_ms < earliest) earliest = a.at_ms;
        if (a.at_ms > latest) latest = a.at_ms;
    }
    // The whole slot is usable: an open dwell is a tuned dwell, so 450 itself is
    // a legal instant and no guard is owed at the front.
    CHECK(earliest == kDirectStart);
    CHECK(latest ==
          kSlot0End - Transmitter::kCompletionSlackMs - static_cast<int>(Transmitter::kAirTimeMs));
}

// The upper channel's dwell opens at 800 already tuned - the hop guard before it
// is what paid for the retune - so 800 is its first legal instant.
TEST_CASE("transmit: the first instant of a dwell is the moment it opens") {
    Transmitter t = airborne_transmitter();
    int earliest = 2000, latest = 0;
    for (uint32_t utc = 1; utc < 5000; utc += 2) {
        const Transmitter::Attempt a = t.attempt(slot_plan(900), utc, utc * 1000, true, 0);
        REQUIRE(a.go);
        if (a.at_ms < earliest) earliest = a.at_ms;
        if (a.at_ms > latest) latest = a.at_ms;
    }
    CHECK(earliest == kSlot1Start);
    CHECK(latest ==
          kDirectEnd - Transmitter::kCompletionSlackMs - static_cast<int>(Transmitter::kAirTimeMs));
}

TEST_CASE("transmit: two devices do not pick the same instant every second") {
    Transmitter a = airborne_transmitter(0x5B7E57);
    Transmitter b = airborne_transmitter(0x123456);
    int same = 0;
    for (uint32_t utc = 0; utc < 100; utc += 2) {
        if (a.attempt(slot_plan(500), utc, utc * 1000, true, 0).at_ms ==
            b.attempt(slot_plan(500), utc, utc * 1000, true, 0).at_ms)
            same++;
    }
    CHECK(same < 5);
}

// Two addresses differ by a fixed XOR delta forever: a mixer that carried it would marry the pair.
TEST_CASE("transmit: a shared instant in one second is a fresh draw in the next") {
    constexpr uint32_t kOwn = 0x5B7E57;
    constexpr uint32_t kPeer = 0x5B01FF;
    CHECK(instant_at(kOwn, 1) == instant_at(kPeer, 1));
    // 631 and 560, fourteen burst lengths apart, from the pair that shared 903 ms a second earlier.
    CHECK(instant_at(kOwn, 2) - instant_at(kPeer, 2) == 71);

    const int shared_ms = instant_at(kOwn, 1);
    const int own_next_ms = instant_at(kOwn, 2);
    int shared = 0, still_overlapping = 0;
    for (uint32_t addr = 0x5B0000; addr < 0x5C0000; addr++) {
        if (addr == kOwn || instant_at(addr, 1) != shared_ms) continue;
        shared++;
        if (bursts_overlap(own_next_ms, instant_at(addr, 2))) still_overlapping++;
    }
    REQUIRE(shared > 100);
    // 11 of 202, the odds of two 5 ms bursts meeting in a 341 ms slot. A carried delta keeps 202.
    CHECK(still_overlapping * 10 < shared);
}

// §C.2.5: traffic alternates between the two M-band channels.
// The second dwell hears traffic past 1000 ms. The direct slot ends there, so
// our own burst never does.
TEST_CASE("transmit: the burst completes inside the direct slot, tail or no tail") {
    Transmitter t = airborne_transmitter();
    for (uint32_t utc = 1; utc < 400; utc += 2) {  // the odd seconds are the upper channel's
        const Transmitter::Attempt a = t.attempt(slot_plan(900), utc, utc * 1000, true, 0);
        REQUIRE(a.go);
        CHECK(a.freq_hz == kMband1Hz);
        CHECK(a.at_ms >= kSlot1Start);
        CHECK(a.at_ms + static_cast<int>(Transmitter::kAirTimeMs) +
                  Transmitter::kCompletionSlackMs <=
              kDirectEnd);
    }
    // And the tail is not a position opportunity, however anchored the clock is.
    CHECK_FALSE(position_burst(t.attempt(slot_plan(100), 500, 500000, true, 0)));
}

TEST_CASE("transmit: consecutive transmissions alternate channel and slot") {
    Transmitter t = airborne_transmitter();
    CHECK(t.attempt(slot_plan(500), 10, 10000, true, 0).freq_hz == kMband0Hz);
    CHECK_FALSE(position_burst(t.attempt(slot_plan(900), 10, 10000, true, 0)));
    t.sent(10, 10500);
    CHECK_FALSE(t.attempt(slot_plan(500), 11, 11000, true, 0).go);
    CHECK(t.attempt(slot_plan(900), 11, 11000, true, 0).freq_hz == kMband1Hz);
}

// A burst refused for any reason used to shift the alternation for ever after.
TEST_CASE("transmit: a missed transmission does not put the channel out of step") {
    Transmitter t = airborne_transmitter();
    CHECK(t.attempt(slot_plan(500), 10, 10000, true, 0).freq_hz == kMband0Hz);
    CHECK(t.attempt(slot_plan(500), 12, 12000, true, 0).freq_hz == kMband0Hz);
    CHECK(t.attempt(slot_plan(900), 13, 13000, true, 0).freq_hz == kMband1Hz);
}

// §G.1.16: at least 1 Hz airborne, 0.1 Hz on the ground, one second of UTC's own ten.
TEST_CASE("transmit: one burst per second airborne, one per ten on the ground") {
    Transmitter t = airborne_transmitter();
    t.sent(10, 10500);
    CHECK_FALSE(position_burst(t.attempt(slot_plan(900), 10, 10800, true, 0)));
    CHECK(t.attempt(slot_plan(900), 11, 11000, true, 0).go);

    Transmitter g = airborne_transmitter();
    const uint32_t owned = g.ground_second();
    for (uint32_t utc = owned + 1; utc < owned + 10; utc++) {
        CAPTURE(utc);
        CHECK_FALSE(position_burst(g.attempt(slot_plan(500), utc, utc * 1000, false, 0)));
        CHECK_FALSE(position_burst(g.attempt(slot_plan(900), utc, utc * 1000, false, 0)));
    }
    const int phase = Transmitter::slot_in(owned + 10, false) == 0 ? 500 : 900;
    CHECK(g.attempt(slot_plan(phase), owned + 10, (owned + 10) * 1000, false, 0).go);
}

TEST_CASE("transmit: the callsign goes out in a second of its own, once every ten") {
    Transmitter t = airborne_transmitter();
    const uint32_t owned = t.callsign_second();
    const SlotPlan tail = slot_plan(kSlot1Start);

    for (uint32_t utc = owned + 1; utc < owned + Transmitter::kCallsignPeriodS; utc++) {
        CAPTURE(utc);
        CHECK(t.attempt(tail, utc, utc * 1000, true, 0).payload != Transmitter::Payload::Callsign);
    }

    const uint32_t due = owned + Transmitter::kCallsignPeriodS;
    const Transmitter::Attempt a = t.attempt(tail, due, due * 1000, true, 0);
    REQUIRE(a.go);
    CHECK(a.payload == Transmitter::Payload::Callsign);
    CHECK(a.freq_hz == kMband1Hz);
    CHECK(a.at_ms >= kCallsignStart);
    CHECK(a.at_ms + static_cast<int>(Transmitter::kAirTimeMs) <= kCallsignEnd);

    t.sent(due, due * 1000, Transmitter::Payload::Callsign);
    CHECK_FALSE(t.attempt(tail, due, due * 1000, true, 0).go);
}

// Half of all addresses once lost every name to their own slot-1 position burst.
TEST_CASE("transmit: every address names itself every ten seconds, airborne or on the ground") {
    for (uint32_t addr = 0x5B0000; addr < 0x5B0400; addr++) {
        for (const bool airborne : {true, false}) {
            CAPTURE(addr);
            CAPTURE(airborne);
            Transmitter t = airborne_transmitter(addr);
            int named = 0;
            for (uint32_t utc = 100; utc < 100 + 2 * Transmitter::kCallsignPeriodS; utc++) {
                for (const int phase : {500, kSlot1Start}) {
                    const Transmitter::Attempt a =
                        t.attempt(slot_plan(phase), utc, utc * 1000, airborne, 0);
                    if (!a.go) continue;
                    t.sent(utc, utc * 1000 + static_cast<uint32_t>(phase), a.payload);
                    if (a.payload == Transmitter::Payload::Callsign) named++;
                }
            }
            CHECK(named == 2);
        }
    }
}

// The position burst is the one G.1.16 dates, and a name carries no instant to be stale.
TEST_CASE("transmit: a late solution refuses the position burst and not the callsign") {
    Transmitter t = airborne_transmitter();
    const uint32_t due = t.callsign_second() + Transmitter::kCallsignPeriodS;
    const int32_t late = Transmitter::kFixLagMaxMs + 1;

    CHECK_FALSE(t.attempt(slot_plan(500), due, due * 1000, true, late).go);
    const Transmitter::Attempt a = t.attempt(slot_plan(kSlot1Start), due, due * 1000, true, late);
    REQUIRE(a.go);
    CHECK(a.payload == Transmitter::Payload::Callsign);
}

// Slot 0 is the other channel and the direct slot's own half of the second.
TEST_CASE("transmit: the callsign burst belongs to slot 1 and to no other dwell") {
    Transmitter t = airborne_transmitter();
    const uint32_t due = t.callsign_second() + Transmitter::kCallsignPeriodS;
    t.sent(due, due * 1000);
    CHECK_FALSE(t.attempt(slot_plan(kSlot0Start), due, due * 1000, true, 0).go);
    CHECK(t.attempt(slot_plan(kSlot1Start), due, due * 1000, true, 0).go);
}

// A device that reboots comes back to the same second of the ten, not to a new one.
TEST_CASE("transmit: the ground second is the address's, not the boot's") {
    CHECK(airborne_transmitter(0x5B7E57).ground_second() ==
          airborne_transmitter(0x5B7E57).ground_second());

    int taken[Transmitter::kGroundPeriodS] = {0};
    for (uint32_t addr = 0x5B0000; addr < 0x5B1000; addr++)
        taken[airborne_transmitter(addr).ground_second()]++;
    for (uint32_t second = 0; second < Transmitter::kGroundPeriodS; second++) {
        CAPTURE(second);
        CHECK(taken[second] > 0x1000 / 20);
    }
}

// §G.1.16, measured to the top of the transmit second: what it refuses is a missed solution.
TEST_CASE("transmit: a missed solution is not transmitted, a late slot is") {
    Transmitter t = airborne_transmitter();
    CHECK(t.attempt(slot_plan(500), 10, 10000, true, 500).go);
    CHECK_FALSE(t.attempt(slot_plan(500), 10, 10000, true, 501).go);

    // This second's own solution, however late in the slot the burst goes out.
    CHECK(t.attempt(slot_plan(750), 10, 10750, true, 0).go);

    // A solution stamped after the second it opens: early, not stale.
    CHECK(t.attempt(slot_plan(500), 10, 10000, true, -20).go);
}

TEST_CASE("transmit: nothing goes out unless the slot allows it") {
    Transmitter t = airborne_transmitter();
    Scheduler s;
    ClockState no_pps{true, false, 0};
    CHECK_FALSE(t.attempt(s.plan(500, no_pps), 10, 10000, true, 0).go);
    CHECK_FALSE(t.attempt(s.plan(300, anchored()), 10, 10000, true, 0).go);
    // Claimed from 400, but the instant is what waits for the direct slot to open at 450.
    const Transmitter::Attempt early = t.attempt(s.plan(420, anchored()), 10, 10000, true, 0);
    CHECK(early.go);
    CHECK(early.at_ms >= kDirectStart);
}

TEST_CASE("transmit: own-ship goes on air only with a settled fix on an anchored clock") {
    skyblip::model::OwnState own{};
    own.fix_valid = true;
    own.utc_valid = true;
    own.tx_settled = true;
    CHECK(own_ship_transmits(own, anchored()));

    CHECK_FALSE(own_ship_transmits(own, ClockState{true, false, 0}));
    CHECK_FALSE(own_ship_transmits(own, ClockState{false, true, 0}));

    skyblip::model::OwnState unfixed = own;
    unfixed.fix_valid = false;
    CHECK_FALSE(own_ship_transmits(unfixed, anchored()));

    skyblip::model::OwnState untimed = own;
    untimed.utc_valid = false;
    CHECK_FALSE(own_ship_transmits(untimed, anchored()));

    skyblip::model::OwnState unsettled = own;
    unsettled.tx_settled = false;
    CHECK_FALSE(own_ship_transmits(unsettled, anchored()));
}

// E1. Nothing gates a burst on the channel, but a pilot still reads how loud the site is.

TEST_CASE("channel: the floor starts at the seed and walks to what the receiver hears") {
    NoiseFloor floor;
    CHECK(floor.dbm() == NoiseFloor::kSeedDbm);
    CHECK(floor.samples() == 0);

    for (int i = 0; i < 500; i++) floor.sample(-118);
    CHECK(floor.dbm() == -118);
    CHECK(floor.samples() == 500);

    // And back up again: the average has no memory of having been quiet.
    for (int i = 0; i < 500; i++) floor.sample(-96);
    CHECK(floor.dbm() == -96);
}

// A burst passing through does not become the floor. A minute of a jammer does.
TEST_CASE("channel: one loud sample barely moves the average, a site full of them moves it all") {
    NoiseFloor floor;
    floor.sample(-40);
    CHECK(floor.dbm() <= NoiseFloor::kSeedDbm + 4);

    NoiseFloor site;
    for (int i = 0; i < 200; i++) site.sample(-85);
    CHECK(site.dbm() == -85);
}

// The chip has no averaging block and no non-LoRa activity mode, so a level is a run of reads.
TEST_CASE("channel: one level is a window of readings, not the instant one landed on") {
    CHECK(ChannelLevel::kWindowUs == 160);
    CHECK(ChannelLevel::kSamples >= 2);
    CHECK(ChannelLevel::kSampleSpacingUs * (ChannelLevel::kSamples - 1) >= ChannelLevel::kWindowUs);
    CHECK(ChannelLevel::kSamples <= ChannelLevel::kMaxSamples);
}

TEST_CASE("channel: a window is averaged as power, not as decibels") {
    const int8_t flat[ChannelLevel::kSamples] = {-100, -100, -100, -100, -100,
                                                 -100, -100, -100, -100};
    CHECK(ChannelLevel::mean_dbm(flat, ChannelLevel::kSamples) == -100);

    // One eighth of a window at -60 and the rest 40 dB down: the mean POWER is
    // 1/9 of the loud sample, which is 9.5 dB below it. The mean of the READINGS
    // would be -95.6 dBm, which is a channel nobody is using.
    const int8_t burst[ChannelLevel::kSamples] = {-100, -100, -100, -100, -60,
                                                  -100, -100, -100, -100};
    CHECK(ChannelLevel::mean_dbm(burst, ChannelLevel::kSamples) == -69);

    // Rounding is toward the louder decibel, so a loud site never reads quiet.
    const int8_t pair[2] = {-70, -70};
    CHECK(ChannelLevel::mean_dbm(pair, 2) == -70);
    const int8_t half[2] = {-70, -127};
    CHECK(ChannelLevel::mean_dbm(half, 2) == -73);

    // No reading at all is not a quiet channel.
    CHECK(ChannelLevel::mean_dbm(flat, 0) == 0);
    CHECK(ChannelLevel::mean_dbm(nullptr, ChannelLevel::kSamples) == 0);
}

// A floor fed single reads reports the gaps between bursts as the site's level.
TEST_CASE("channel: the floor hears the window, not the instant one read landed on") {
    const int8_t window[ChannelLevel::kSamples] = {-110, -110, -110, -110, -55,
                                                   -110, -110, -110, -110};
    NoiseFloor windowed, instant;
    for (int i = 0; i < 400; i++) {
        windowed.sample(ChannelLevel::mean_dbm(window, ChannelLevel::kSamples));
        instant.sample(window[0]);
    }
    CHECK(instant.dbm() == window[0]);
    CHECK(windowed.dbm() > instant.dbm() + 40);
}

// E2. The 1% duty cycle of EN 300 220-2 V3.3.1 Table 4 is the declared route, and the only refusal.

TEST_CASE("channel: the declared route is the duty cycle, and nothing else refuses a burst") {
    CHECK(AirTime::kLimitPermille == 10);
    CHECK(AirTime::kBudgetMs == AirTime::kWindowMs / 100);

    AirTime air;
    air.spend(0, AirTime::kBudgetMs);
    CHECK_FALSE(air.may_spend(0, Transmitter::kAirTimeMs));
    // A second later the hour still holds every millisecond of it.
    CHECK_FALSE(air.may_spend(1000, Transmitter::kAirTimeMs));
}

TEST_CASE("channel: air time is counted per rolling hour and leaves it again") {
    AirTime air;
    for (uint32_t second = 0; second < 3600; second++)
        air.spend(second * 1000, Transmitter::kAirTimeMs);

    CHECK(air.bursts() == 3600);
    CHECK(air.total_ms() == 3600 * Transmitter::kAirTimeMs);
    CHECK(air.window_ms(3599000) == 18000);
    // One 5 ms burst a second is 0.5%: half of what the band allows.
    CHECK(air.permille(3599000) == 5);
    CHECK(air.permille(3599000) * 2 == AirTime::kLimitPermille);
    // An hour after the last burst the window is empty, and the total is not.
    CHECK(air.window_ms(3599000 + AirTime::kWindowMs) == 0);
    CHECK(air.total_ms() == 18000);
}

TEST_CASE("channel: the hour's allowance is 1% of it, and the counter stops at the figure") {
    CHECK(AirTime::kBudgetMs == AirTime::kWindowMs / 100);
    CHECK(AirTime::kBudgetMs == 36000);

    AirTime air;
    uint32_t refused = 0;
    for (uint32_t t = 0; t < AirTime::kWindowMs; t += 100) {
        if (air.may_spend(t, Transmitter::kAirTimeMs))
            air.spend(t, Transmitter::kAirTimeMs);
        else
            refused++;
    }
    CHECK(air.window_ms(AirTime::kWindowMs - 100) == AirTime::kBudgetMs);
    CHECK(refused > 0);
    CHECK_FALSE(air.may_spend(AirTime::kWindowMs - 100, Transmitter::kAirTimeMs));
}

// A transmitter at twice the design rate is faulted, and a faulted one that will not stop is worse.
TEST_CASE("transmit: an exhausted hour blocks the burst and says so") {
    Transmitter t = airborne_transmitter();
    for (uint32_t i = 0; i < 7200; i++) t.sent(i, i * 500);
    REQUIRE(t.air_time().window_ms(3599500) == AirTime::kBudgetMs);

    const Transmitter::Attempt a = t.attempt(slot_plan(500), 7200, 3599500, true, 0);
    CHECK_FALSE(a.go);
    CHECK(a.over_budget);
}

TEST_CASE("transmit: the design rate never reaches the limit, so nothing is ever blocked") {
    Transmitter t = airborne_transmitter();
    for (uint32_t i = 0; i < 3600; i++) t.sent(i, i * 1000);
    CHECK(t.air_time().permille(3599000) == 5);
    const Transmitter::Attempt a = t.attempt(slot_plan(500), 3600, 3600000, true, 0);
    CHECK(a.go);
    CHECK_FALSE(a.over_budget);
}

// M. ports::Clock::millis() wraps every 49.7 days and this device flies through
// that instant. The wrap is a value, not a wait: every case below sets the clock
// a few hundred milliseconds short of 0xFFFFFFFF and steps past it.
namespace {
constexpr uint32_t kBeforeWrap = 0xFFFFFC00u;  // 1024 ms short of the wrap
}

TEST_CASE("transmit: the ground schedule is UTC's, so the millisecond wrap cannot move it") {
    Transmitter g = airborne_transmitter();
    const uint32_t owned = g.ground_second();
    const int phase = Transmitter::slot_in(owned, false) == 0 ? 500 : 900;
    g.sent(owned - 10, kBeforeWrap);
    CHECK(g.attempt(slot_plan(phase), owned, 1024u, false, 0).go);
    CHECK_FALSE(g.attempt(slot_plan(phase), owned + 1, 2024u, false, 0).go);
}

// The regulatory one. EN 300 220-2 V3.3.1 Table 4 band M is 1% of any hour, and
// the hour that straddles the wrap is an hour like any other. Buckets numbered
// now_ms / kBucketMs cannot do this: that number restarts at zero at the wrap and
// 2^32 ms is not a whole number of minutes either, so the ring dropped everything
// it held at the wrap and the following hour could spend the allowance a second
// time. The ring turns by elapsed time now.
TEST_CASE("channel: the rolling duty-cycle hour spans the 49.7-day wrap") {
    AirTime air;
    // Half an hour of the design rate up to the wrap, half an hour after it.
    const uint32_t start = 0xFFFFFFFFu - 1800u * 1000u + 1u;
    for (uint32_t i = 0; i < 3600; i++) air.spend(start + i * 1000u, Transmitter::kAirTimeMs);

    const uint32_t last = start + 3599u * 1000u;
    CHECK(air.bursts() == 3600);
    CHECK(air.total_ms() == 3600 * Transmitter::kAirTimeMs);
    // Every one of the 3600 bursts is inside the hour that ends at the last one,
    // whichever side of zero it was spent on. Before the fix this read 9000: the
    // half hour before the wrap had been forgotten.
    CHECK(air.window_ms(last) == 18000);
    CHECK(air.permille(last) == 5);
    // So the budget is still the band's, and a device at twice the design rate
    // through the wrap is still refused at 1%.
    AirTime hot;
    uint32_t spent = 0;
    for (uint32_t i = 0; i < 36000; i++) {
        const uint32_t at = start + i * 100u;
        if (!hot.may_spend(at, Transmitter::kAirTimeMs)) continue;
        hot.spend(at, Transmitter::kAirTimeMs);
        spent += Transmitter::kAirTimeMs;
    }
    CHECK(spent <= AirTime::kBudgetMs);
    CHECK(hot.window_ms(start + 35999u * 100u) == AirTime::kBudgetMs);
}

TEST_CASE("channel: air time spent before the wrap leaves the hour after it") {
    AirTime air;
    air.spend(0xFFFFF000u, 1000);
    CHECK(air.window_ms(0xFFFFF000u) == 1000);
    // Still inside the hour, 59 minutes and 55 seconds later, past the wrap.
    CHECK(air.window_ms(0xFFFFF000u + AirTime::kWindowMs - 5000u) == 1000);
    // And out of it, five seconds after that.
    CHECK(air.window_ms(0xFFFFF000u + AirTime::kWindowMs) == 0);
    CHECK(air.total_ms() == 1000);
}

// A device parked for a day and then flown: the ring has no bucket the hour can
// reach, so it holds nothing, and the total it has spent since boot is untouched.
TEST_CASE("channel: a silence longer than the window empties it and keeps the total") {
    AirTime air;
    air.spend(1000, 5);
    air.spend(1000 + 24u * 3600u * 1000u, 5);
    CHECK(air.window_ms(1000 + 24u * 3600u * 1000u) == 5);
    CHECK(air.total_ms() == 10);
    CHECK(air.bursts() == 2);
}
