// What becomes of a burst after it was armed: a late TxDone credited to the burst it names, and a
// report that never comes counted missed once it is overdue.
#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "core/events/rf.h"
#include "core/model/band.h"
#include "core/timing/channel.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/identity.h"
#include "products/common/services/traffic.h"
#include "products/skyblip_go/settings.h"
#include "test/support/armings_rig.h"

using namespace skyblip;

namespace {

constexpr uint64_t kSecondOneUs = Armings::kEdgeUs + 1000000;
constexpr uint64_t kBurstUs = 6000;

struct Outcomes {
    Armings& armings;
    uint64_t held_at_us{0};
    std::vector<uint64_t> reported{};

    void report_flown() {
        const uint64_t now_us = armings.clock.microseconds();
        for (uint32_t i = 0; i < armings.rf.handed; i++) {
            const ports::RfPlan& plan = armings.rf.plans[i];
            if (plan.tx == nullptr || plan.tx_at_us + kBurstUs > now_us) continue;
            if (plan.tx_at_us != held_at_us && !was_reported(plan.tx_at_us)) report(plan.tx_at_us);
        }
    }

    void report(uint64_t tx_at_us) {
        armings.state.rf.note_tx_done(armings.state.air.tx_ok, tx_at_us);
        armings.state.air.tx_ok++;
        reported.push_back(tx_at_us);
    }

    bool was_reported(uint64_t tx_at_us) const {
        return std::find(reported.begin(), reported.end(), tx_at_us) != reported.end();
    }

    uint64_t name_at_us() const {
        for (uint32_t i = 0; i < armings.rf.handed; i++) {
            const ports::RfPlan& plan = armings.rf.plans[i];
            if (plan.tx != nullptr && plan.tx_at_us >= kSecondOneUs - 200000 &&
                plan.tx_at_us < kSecondOneUs)
                return plan.tx_at_us;
        }
        return 0;
    }
};

void fly_ground_second(Armings& armings, Outcomes& outcomes) {
    std::strncpy(armings.settings.callsign, "D-KXYZ", go::kCallsignBytes - 1);
    armings.clock.set_microseconds(Armings::kEdgeUs);
    REQUIRE(armings.radio.setup() == Status::Ok);
    uint32_t utc_s = 1000;
    while (!armings.radio.transmitter().callsign_second(utc_s) ||
           timing::Transmitter::extra_slot_in(utc_s) != 1)
        utc_s++;
    armings.ready_to_transmit(utc_s);
    armings.state.clock.utc_s = utc_s;
    for (int phase = 0; phase < 1000; phase += 10) {
        armings.tick_at(phase);
        if (outcomes.held_at_us == 0) outcomes.held_at_us = outcomes.name_at_us();
        outcomes.report_flown();
    }
    REQUIRE(outcomes.held_at_us != 0);
    armings.state.own.utc_s = utc_s + 1;
    armings.state.clock.utc_s = utc_s + 1;
}

}  // namespace

// A slot-1 name's TxDone can reach the policy after the second turns: counted missed then, it was
// once credited to the next dwell as a position.
TEST_CASE("rf: a TxDone that lands after its dwell closed is credited to the burst it names") {
    Armings armings;
    Outcomes outcomes{armings};
    fly_ground_second(armings, outcomes);

    for (int phase = 0; phase <= 200; phase += 10) {
        armings.tick_in(1, phase);
        outcomes.report_flown();
    }
    outcomes.report(outcomes.held_at_us);
    armings.tick_in(1, 210);

    CHECK(armings.state.rf.timing_statistics.missed() == 0);
    CHECK(armings.state.air.tx_named == 1);
    CHECK(armings.radio.transmitter().sent_count() == outcomes.reported.size());
}

TEST_CASE("rf: a burst that never reports is counted missed once its report is overdue") {
    Armings armings;
    Outcomes outcomes{armings};
    fly_ground_second(armings, outcomes);

    const int overdue_ms =
        timing::kSlot1WrapMs + static_cast<int>(go::RadioService::kTxOutcomeMaxAgeMs);
    for (int phase = 0; phase < overdue_ms; phase += 10) {
        armings.tick_in(1, phase);
        outcomes.report_flown();
    }
    CHECK(armings.state.rf.timing_statistics.missed() == 0);

    armings.tick_in(1, overdue_ms);
    CHECK(armings.state.rf.timing_statistics.missed() == 1);
    CHECK(armings.state.air.tx_named == 0);
}
