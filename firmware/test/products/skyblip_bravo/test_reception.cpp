// A full capture on Bravo says each second what the LR2021 heard and what Bravo made of it.
#include <algorithm>
#include <vector>

#include "core/diagnostics/payload.h"
#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"
#include "test/support/diagnostics_corpus.h"
#include "test/support/diagnostics_read.h"
#include "test/support/diagnostics_taps.h"

using namespace skyblip;

namespace {

void arm(BravoRig& rig, diagnostics::Profile profile = diagnostics::Profile::Full) {
    rig.product.diagnostics().arm(profile);
    rig.run_for(100);
    REQUIRE(rig.product.capture().capturing());
}

std::vector<diagnostics::Record> stop(BravoRig& rig) {
    rig.product.diagnostics().disarm();
    rig.run_for(4000);
    REQUIRE_FALSE(rig.product.capture().capturing());
    return captured(rig);
}

void hear_corrupted(BravoRig& rig) {
    AdsbAir corrupted = adsb_air(adsb::airborne_position(0x3C6586, 0, 48.51, 8.5, false, 1800));
    corrupted.chips[10] ^= 0x0C;
    rig.platform.radio().receive_air(corrupted.chips, sizeof(corrupted.chips));
    rig.step();
}

}  // namespace

TEST_CASE("bravo reception: a full capture records the receiver and the fold once a second") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(2);
    arm(rig);
    hear_corrupted(rig);
    rig.hear_aircraft(0x4CA2B1, 3000);
    rig.seconds_with_fix(5);
    const std::vector<diagnostics::Record> records = stop(rig);

    CHECK(count_of(records, diagnostics::Type::Receiver) >= 5);
    CHECK(count_of(records, diagnostics::Type::Receiver) <= 7);
    CHECK(count_of(records, diagnostics::Type::Reception) ==
          count_of(records, diagnostics::Type::Receiver));
    CHECK(count_of(records, diagnostics::Type::Framing) ==
          count_of(records, diagnostics::Type::Receiver));
    diagnostics::Framing framing{};
    REQUIRE(last_of(records, framing));
    CHECK(framing.overflow_drops == 0);
    CHECK(framing.resync_drops == 0);
    CHECK(framing.queue_drops == 0);
    CHECK(framing.drains >= 2);
    CHECK(framing.drain_busy_ms == 0);
    CHECK(count_of(records, diagnostics::Type::Screening) ==
          count_of(records, diagnostics::Type::Receiver));
    diagnostics::Screening screening{};
    REQUIRE(last_of(records, screening));
    CHECK(screening.squitters == 2);
    CHECK(screening.parity_refused == 1);
    CHECK(screening.one_bit_errors == 1);
    CHECK(screening.all_calls == 0);
    CHECK(screening.replies == 0);
    uint32_t fifo_peak = 0;
    for (const diagnostics::Record& record : records) {
        diagnostics::Screening second{};
        if (diagnostics::read(record, second))
            fifo_peak = std::max(fifo_peak, second.fifo_peak_frames);
    }
    CHECK(fifo_peak >= 1);

    diagnostics::Receiver receiver{};
    REQUIRE(last_of(records, receiver));
    CHECK(receiver.frames == 3);
    CHECK(receiver.part_packets == 3);
    CHECK(receiver.listening);
    CHECK(receiver.probed);
    CHECK(receiver.part_mode == ports::ReceiverMode::Receive);
    CHECK(receiver.part_reset == ports::ReceiverReset::None);
    CHECK(receiver.threshold_dbm == parts::lr2021::kDetectionThresholdDbm);
    CHECK(receiver.front_end_kept);
    CHECK(receiver.noise_dbm == -120);
    CHECK(receiver.gain_step == parts::lr2021::kGainStep);

    diagnostics::Reception reception{};
    REQUIRE(last_of(records, reception));
    CHECK(reception.crc == 1);
    CHECK(reception.folded == 2);
    CHECK(reception.positions == 1);
    CHECK(reception.aircraft == 1);
    CHECK(reception.tracked == 1);
    CHECK(reception.reference == comms::ExportReference::Own);
    CHECK(reception.fix_valid);
}

// A brown-out makes the LR2021 forget its modem: the capture says so, and Bravo hears again.
TEST_CASE("bravo reception: a brown-out of the LR2021 is recorded, and reception comes back") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(2);
    arm(rig);
    rig.hear_aircraft(0x4CA2B1, 3000);
    rig.seconds_with_fix(1);
    rig.platform.radio().brown_out();
    rig.seconds_with_fix(3);
    rig.hear_aircraft(0x4CA2B1, 3000);
    rig.seconds_with_fix(1);
    const std::vector<diagnostics::Record> records = stop(rig);

    bool reported = false;
    for (const diagnostics::Record& record : records) {
        diagnostics::Receiver receiver{};
        if (!diagnostics::read(record, receiver) || !receiver.probed) continue;
        if (receiver.part_reset != ports::ReceiverReset::PowerOn) continue;
        reported = true;
        CHECK(receiver.part_mode == ports::ReceiverMode::StandbyRc);
        CHECK(receiver.threshold_dbm != parts::lr2021::kDetectionThresholdDbm);
        CHECK_FALSE(receiver.front_end_kept);
        CHECK(receiver.listening);
    }
    CHECK(reported);

    diagnostics::Receiver last{};
    REQUIRE(last_of(records, last));
    CHECK(last.part_resets == 1);
    CHECK(last.resets == 1);
    CHECK(last.part_reset == ports::ReceiverReset::None);
    CHECK(last.part_mode == ports::ReceiverMode::Receive);
    CHECK(last.threshold_dbm == parts::lr2021::kDetectionThresholdDbm);
    CHECK(last.front_end_kept);
    CHECK(last.frames == 4);
}

TEST_CASE("bravo reception: a power run leaves the four records out, as it does every second's") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(2);
    arm(rig, diagnostics::Profile::PowerRun);
    rig.hear_aircraft(0x4CA2B1, 3000);
    rig.seconds_with_fix(3);
    const std::vector<diagnostics::Record> records = stop(rig);
    CHECK(count_of(records, diagnostics::Type::Receiver) == 0);
    CHECK(count_of(records, diagnostics::Type::Reception) == 0);
    CHECK(count_of(records, diagnostics::Type::Framing) == 0);
    CHECK(count_of(records, diagnostics::Type::Screening) == 0);
}
