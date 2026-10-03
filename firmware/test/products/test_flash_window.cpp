// The settings write through the whole product: a change goes in where a pilot or
// a phone makes it, and what comes out is counted writes on the host KvStore.
// Nothing below the services is stubbed, so the dwell the write has to dodge is
// the one the radio service actually armed.
//
// What is being defended is 1-ARCHITECTURE.md §5.1's "no flash work inside a
// dwell". The settings page lets a pilot change alarm volume and the altimeter
// subscale IN FLIGHT, and on the nRF52840 the store behind ports::KvStore is the
// internal storage_partition: a write there is an NVMC stall on the same core that
// arms PPS-anchored deadlines. Before the page existed a settings write could only
// happen on the ground, because a companion "set" is refused airborne.
#include "core/events/link.h"
#include "core/timing/durable_write.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/input/gesture.h"
#include "products/skyblip_go/pages/menu.h"
#include "test/support/product_rig.h"
#include "test/support/settings_writes.h"

using namespace skyblip;
using namespace skyblip::settings_writes;

// The case the finding is about: a value changed with a dwell armed and the
// transmitter allowed on air. The blob must not reach flash there, and when it
// does reach flash the phase it landed on has to be one the policy calls free.
TEST_CASE("flash window: a change made inside a dwell is not written until the window opens") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t);
    step_until(rig, t, t + static_cast<uint32_t>(timing::kDirectStart) + 20);
    REQUIRE(published_phase(rig) >= timing::kDirectStart);
    REQUIRE(rig.state().rf.plan.tx_allowed);
    const uint32_t before = writes(rig);

    change_volume(rig, 4);
    // Through the whole of the dwell it was made in, and the settle behind it,
    // nothing is on flash.
    step_until(rig, t, t + timing::DurableWriteWindow::kSettleMs);
    CHECK(writes(rig) == before);

    const uint32_t at_ms = wait_for_write(rig, t, timing::DurableWriteWindow::kMaxDeferMs);
    REQUIRE(at_ms != 0);
    const int phase = static_cast<int>(at_ms % 1000);
    // One of the two stretches the dwell map leaves: slot 1's tail, or the uplink
    // dwell. Read off the policy rather than restated as numbers.
    CHECK(timing::DurableWriteWindow::free_at(rig.state().rf.plan, phase,
                                              timing::DurableWriteWindow::kWorstWriteMs));
    CHECK(rig.product.config().durable_writes().forced() == 0);
}

// Work earlier in the pass left the settings write deciding on the phase the pass began at.
TEST_CASE("flash window: a settings write is placed at the instant it starts, not its pass") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    stand_on_the_ground(rig, t);
    const uint32_t before = writes(rig);
    change_volume(rig, 4);
    rig.platform.clock().set_millis(t);
    rig.product.config().tick(t);
    REQUIRE(rig.product.config().durable_writes().pending());

    // The last phase of the uplink dwell a worst-case write still clears a guard before its end.
    constexpr int kLastWritePhase = timing::kUplinkRxEnd - timing::kJitterGuardMs -
                                    static_cast<int>(timing::DurableWriteWindow::kWorstWriteMs);
    constexpr int kRanLongMs = 50;
    constexpr int kPassPhase = kLastWritePhase - kRanLongMs;
    const uint32_t pass_ms = t + 1000 + kPassPhase;
    publish_dwell(rig, pass_ms, kPassPhase);
    rig.platform.clock().set_millis(pass_ms + kRanLongMs + 1);
    rig.product.config().tick(pass_ms);
    CHECK(writes(rig) == before);
    CHECK(rig.product.config().durable_writes().pending());

    publish_dwell(rig, pass_ms + 1000, kPassPhase);
    rig.platform.clock().set_millis(pass_ms + 1000 + kRanLongMs);
    rig.product.config().tick(pass_ms + 1000);
    CHECK(writes(rig) - before == 1);
    CHECK(rig.product.config().durable_writes().forced() == 0);
}

// The view was stamped at the pass start and read off the clock later, so projections ran late.
TEST_CASE("flash window: the radio's view of the second is stamped when its phase was read") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.pps().set_locked(false);
    uint64_t us = 10'000'000;
    rig.run_span_from_us(us, 200);
    REQUIRE_FALSE(rig.state().clock.pps_locked);

    constexpr uint32_t kRadioTurnMs = 40;
    const uint32_t pass_ms = rig.platform.clock().millis() + 50;
    rig.platform.clock().set_millis(pass_ms + kRadioTurnMs);
    rig.product.step(pass_ms);

    const timing::DwellPhase& dwell = rig.state().rf.dwell;
    const uint32_t later_ms = pass_ms + 90;
    // Free-running, the phase is the clock's own millisecond within the second.
    CHECK(dwell.at_ms == pass_ms + kRadioTurnMs);
    CHECK((dwell.phase_ms + static_cast<int>(later_ms - dwell.at_ms)) % 1000 ==
          static_cast<int>(later_ms % 1000));
}

// Six taps stepping the alarm volume through six values. One write.
TEST_CASE("flash window: six rapid changes are one write") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t);
    const uint32_t before = writes(rig);

    for (uint8_t volume = 0; volume < 6; volume++) {
        change_volume(rig, volume);
        step_until(rig, t, t + 120);
    }
    REQUIRE(rig.product.config().durable_writes().requests() == 6);

    REQUIRE(wait_for_write(rig, t, timing::DurableWriteWindow::kMaxDeferMs) != 0);
    step_until(rig, t, t + 2000);
    CHECK(writes(rig) - before == 1);
    CHECK(rig.product.config().durable_writes().writes() == 1);

    // And what landed is the last value, not the first: the blob is only read at
    // the instant it is written.
    uint8_t blob[64];
    size_t n = 0;
    REQUIRE(rig.platform.kv().read("settings", blob, sizeof(blob), n) == Status::Ok);
    go::Settings stored{};
    REQUIRE(go::from_blob(blob, n, stored) == Status::Ok);
    CHECK(stored.alarm_volume == 5);
}

// The same six values, stepped by a real thumb on the panel rather than by the
// flag underneath it: presses in at the pin the board polls.
TEST_CASE("flash window: a thumb stepping the volume on the panel writes flash once") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    stand_on_the_ground(rig, t);

    rig.press(t);
    REQUIRE(rig.product.screen().mode() == go::Mode::Menu);
    rig.tap(t);  // off the self test, onto the rows

    // Down to the volume row: a tap of the pad moves the focus.
    while (rig.product.screen().editor().focus() != go::MenuRow::Volume) rig.tap(t);

    const uint32_t before = writes(rig);
    const uint8_t started_at = rig.settings().alarm_volume;
    // Five presses walk the volume off where it started, which is five accepted changes.
    for (int press = 0; press < 5; press++) {
        rig.press(t);
        rig.run(t, t + 120);
        t += 120;
    }
    REQUIRE(rig.settings().alarm_volume != started_at);
    REQUIRE(rig.product.config().durable_writes().requests() == 5);

    rig.run(t, t + 3000);
    t += 3000;
    CHECK(writes(rig) - before == 1);
    CHECK(rig.product.config().durable_writes().forced() == 0);
}

// Whatever phase of the second a change arrives at, it is on flash inside the
// bound - and it is never forced there, because the second offers the window
// twice.
TEST_CASE("flash window: a change is never held past the bound, at any phase") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t);

    uint8_t volume = 0;
    for (int offset = 0; offset < 1000; offset += 70) {
        step_until(rig, t, (t / 1000 + 1) * 1000 + static_cast<uint32_t>(offset));
        const uint32_t asked_at = t;
        volume = static_cast<uint8_t>((volume + 1) % (go::kMaxAlarmVolume + 1));
        change_volume(rig, volume);
        const uint32_t at_ms = wait_for_write(rig, t, timing::DurableWriteWindow::kMaxDeferMs);
        REQUIRE(at_ms != 0);
        CHECK(at_ms - asked_at <= timing::DurableWriteWindow::kMaxDeferMs);
    }
    CHECK(rig.product.config().durable_writes().forced() == 0);
    CHECK(rig.product.config().durable_writes().worst_wait_ms() <
          timing::DurableWriteWindow::kMaxDeferMs);
}

// A change made on the ground still lands promptly. The dwell map runs on the
// ground too - the receiver is armed and the transmitter still reports at 0.1 Hz -
// so this is not the deferral being switched off, it is the same window being wide
// enough that nobody waits for it.
TEST_CASE("flash window: a change made on the ground is written promptly") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    stand_on_the_ground(rig, t);
    REQUIRE(rig.product.config().config().flight_state() == flight::FlightState::Ground);

    const uint32_t asked_at = t;
    change_volume(rig, 3);
    const uint32_t at_ms = wait_for_write(rig, t, timing::DurableWriteWindow::kMaxDeferMs);
    REQUIRE(at_ms != 0);
    // The settle, and less than one whole second of window on top of it.
    CHECK(at_ms - asked_at >= timing::DurableWriteWindow::kSettleMs);
    CHECK(at_ms - asked_at <= timing::DurableWriteWindow::kSettleMs + 1000);
}

// A value stepped up and back down again is not a change to the blob, and NVS
// charges for a write either way: the sector this policy's whole budget is sized
// against must not fill for nothing.
TEST_CASE("flash window: a change that ends where it started writes no flash") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    stand_on_the_ground(rig, t);
    change_volume(rig, 3);
    REQUIRE(wait_for_write(rig, t, timing::DurableWriteWindow::kMaxDeferMs) != 0);

    const uint32_t before = writes(rig);
    change_volume(rig, 5);
    step_until(rig, t, t + 100);
    change_volume(rig, 3);
    step_until(rig, t, t + 3000);
    CHECK(writes(rig) == before);
    // The request was still served: nothing is left pending, so nothing is waiting
    // for a bound it will never spend.
    CHECK_FALSE(rig.product.config().durable_writes().pending());
}

// The bound answers the cell that dies without warning. A power-off does give
// warning, and once the radio has been aborted the second belongs to nobody, so a
// change still waiting for a window goes down with the rails only if we let it.
TEST_CASE("flash window: a pending change is flushed on the way to power off") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t);
    step_until(rig, t, t + static_cast<uint32_t>(timing::kDirectStart) + 20);
    REQUIRE(rig.state().rf.plan.tx_allowed);

    const uint32_t before = writes(rig);
    change_volume(rig, 1);
    step_one(rig, t);
    REQUIRE(writes(rig) == before);
    REQUIRE(rig.product.config().durable_writes().pending());

    rig.product.shutdown().request(power::ShutdownReason::LinkRequest, t);
    step_one(rig, t);
    REQUIRE(rig.product.shutdown().phase() == power::ShutdownPhase::Parking);
    CHECK(writes(rig) - before == 1);
    CHECK_FALSE(rig.product.config().durable_writes().pending());
    // Not a fault: nothing was armed to disturb.
    CHECK(rig.product.config().durable_writes().forced() == 0);
}

// Failure is loud. The counters go out the door the bench already has open: the
// same Config endpoint dispatch that answers "status" and "timing".
TEST_CASE("flash window: the write counters are readable over the companion link") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    stand_on_the_ground(rig, t);
    change_volume(rig, 2);
    REQUIRE(wait_for_write(rig, t, timing::DurableWriteWindow::kMaxDeferMs) != 0);

    rig.platform.link().clear();
    rig.send("{\"cmd\":\"flash\"}");
    step_until(rig, t, t + 100);
    const std::string reply = rig.last_on(events::Endpoint::Config);
    REQUIRE_FALSE(reply.empty());
    CHECK(reply.find("\"cmd\":\"flash\"") != std::string::npos);
    CHECK(reply.find("\"writes\":1") != std::string::npos);
    CHECK(reply.find("\"forced\":0") != std::string::npos);
    CHECK(reply.find("\"worst_wait_ms\":") != std::string::npos);
    // The policy's own numbers, read back off the device rather than off the source.
    CHECK(reply.find("\"budget_ms\":86") != std::string::npos);
    CHECK(reply.find("\"bound_ms\":3000") != std::string::npos);
}

// E2. Rate limit and coalescing, at the scale the finding names: a companion app
// patching a value per keystroke. A hundred changes have to cost one write, and
// one write is what an erase is charged for - NVS appends until the sector is
// full and then garbage-collects the next one, so the number of blob writes is
// the number the wear budget is spent from (core/timing/durable_write.h sizes one
// against a tERASEPAGE plus the live entries copied forward).
TEST_CASE("flash window: a hundred patches cost one write") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t);
    const uint32_t before = writes(rig);
    REQUIRE(rig.settings().alarm_volume == 3);

    // Every pass, which is faster than a thumb and faster than a keystroke, and
    // the whole hundred inside the bound.
    for (int i = 0; i < 100; i++) {
        change_volume(rig, static_cast<uint8_t>((i % 5) + 1));
        step_one(rig, t);
    }
    CHECK(rig.product.config().durable_writes().requests() == 100);
    CHECK(writes(rig) == before);

    REQUIRE(wait_for_write(rig, t, timing::DurableWriteWindow::kMaxDeferMs) != 0);
    step_until(rig, t, t + 2000);
    CHECK(writes(rig) - before == 1);
    CHECK(rig.product.config().durable_writes().writes() == 1);

    // And what one write cost is the hundredth value, not the first.
    uint8_t blob[64];
    size_t n = 0;
    REQUIRE(rig.platform.kv().read("settings", blob, sizeof(blob), n) == Status::Ok);
    go::Settings stored{};
    REQUIRE(go::from_blob(blob, n, stored) == Status::Ok);
    CHECK(stored.alarm_volume == 5);
}

// A refused write was counted as written, and the pilot's change was gone at the next boot.
TEST_CASE("flash window: a write the store refused is retried until it lands") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t);
    rig.platform.kv().refuse_writes = true;
    change_volume(rig, 5);
    step_until(rig, t, t + 4 * timing::DurableWriteWindow::kMaxDeferMs);
    CHECK(rig.product.config().failed_writes() > 0);
    // Retried in the windows it is given, and forced at most once per bound.
    CHECK(rig.product.config().durable_writes().forced() <= 4);
    CHECK(rig.product.config().durable_writes().pending());

    rig.platform.kv().refuse_writes = false;
    step_until(rig, t, t + 2 * timing::DurableWriteWindow::kMaxDeferMs);
    CHECK_FALSE(rig.product.config().durable_writes().pending());
    uint8_t blob[64];
    size_t n = 0;
    REQUIRE(rig.platform.kv().read("settings", blob, sizeof(blob), n) == Status::Ok);
    go::Settings stored{};
    REQUIRE(go::from_blob(blob, n, stored) == Status::Ok);
    CHECK(stored.alarm_volume == 5);
}

TEST_CASE("flash window: a refused write is neither a write nor a change on the bench") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t);
    const timing::DurableWriteWindow& window = rig.product.config().durable_writes();
    const uint32_t requests_before = window.requests();
    const uint32_t writes_before = window.writes();
    rig.platform.kv().refuse_writes = true;
    change_volume(rig, 5);
    step_until(rig, t, t + 4 * timing::DurableWriteWindow::kMaxDeferMs);
    REQUIRE(rig.product.config().failed_writes() > 1);
    CHECK(window.writes() == writes_before);

    rig.platform.kv().refuse_writes = false;
    step_until(rig, t, t + 2 * timing::DurableWriteWindow::kMaxDeferMs);
    CHECK(window.writes() == writes_before + 1);
    CHECK(window.requests() == requests_before + 1);
}

TEST_CASE("flash window: a store that did not mount is never written, and nothing is owed to it") {
    Rig rig;
    rig.platform.kv().refuse_mount = true;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t);
    change_volume(rig, 5);
    step_until(rig, t, t + 2 * timing::DurableWriteWindow::kMaxDeferMs);
    CHECK(rig.platform.kv().writes() == 0);
    CHECK(rig.product.config().failed_writes() == 0);
    CHECK_FALSE(rig.product.config().durable_writes().pending());
}
