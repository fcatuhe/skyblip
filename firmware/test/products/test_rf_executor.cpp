// Below the service list: the SX1262 driver, the host executor that arms and
// services it, and the two decisions they own between them. When the PA may key
// is one - the channel has to be listened to first and the chip's own SetTx
// timeout is what unkeys it - and when the transmit instant is, measured from
// the PPS edge the board latched rather than from a phase that went stale while
// the services ahead of the radio ran. Nothing here is mocked: the chip model
// answers over SPI exactly as the part does, including when it answers nothing
// at all.
#include <initializer_list>

#include "core/events/rf.h"
#include "core/model/band.h"
#include "core/timing/channel.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/services/traffic.h"
#include "products/skyblip_go/settings.h"
#include "simulator/simulator.h"

using namespace skyblip;

namespace {

// The radio policy and its executor with the board's job done by hand, so that
// the two halves of a service pass - the instant the PPS phase is read off the
// hardware, and the instant the policy uses it - can be pulled apart. On silicon
// they are separated by however long the services ahead of the radio took.
// Every part below the services is the real one: the driver, the chip model, the
// executor and the service that turns radio events into counters.
struct Pass {
    platform::host::Platform platform{};
    models::Sx1262& chip{platform.chips().radio};
    parts::Sx1262 radio{chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin};
    bus::Bus bus{};
    bus::State state{};
    platform::host::Rf rf{radio, platform.clock(), bus.rf};
    ports::NullRoles null{};
    ports::Roles roles{platform.clock(), rf,        null.link,
                       null.display,     null.kv,   null.log_flash,
                       null.annunciator, null.dfu,  null.die_temperature,
                       null.indicator,   null.gnss, ports::Capability::Rf,
                       0x5B7E57};
    diag::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    go::Settings settings{};
    go::RadioService radio_service{context, settings};
    go::TrafficService traffic_service{context, go::kFeatures};

    Status begin() {
        const Status s = rf.begin();
        if (s != Status::Ok) return s;
        state.own.fix_valid = true;
        state.own.utc_valid = true;
        state.own.tx_settled = true;
        state.own.flight_state = 2;
        state.clock.utc_valid = true;
        return radio_service.setup();
    }

    // Exactly what boards/lilygo/t_echo_plus/board.h does at the top of a pass.
    void poll_clock(uint64_t now_us) {
        platform.clock().set_micros(now_us);
        state.clock.pps_locked = platform.pps().locked();
        state.clock.ms_since_pps = platform.pps().ms_since(now_us);
        state.clock.pps_edge_us = platform.pps().last_edge_us();
    }

    void whole_pass(uint64_t now_us) {
        poll_clock(now_us);
        services_at(now_us);
    }

    void services_at(uint64_t now_us) {
        platform.clock().set_micros(now_us);
        const uint32_t now_ms = static_cast<uint32_t>(now_us / 1000);
        state.own.utc = now_ms / 1000;
        state.own.fix_ms = now_ms;
        rf.service(now_ms);
        radio_service.tick(now_ms);
        traffic_service.tick(now_ms);
    }

    // The executor between service passes: it runs on its own thread on silicon,
    // so it sees time the service list does not.
    void executor_until(uint64_t until_us, uint64_t step_us = 100) {
        for (uint64_t t = platform.clock().micros() + step_us; t <= until_us; t += step_us) {
            platform.clock().set_micros(t);
            rf.service(static_cast<uint32_t>(t / 1000));
            if (chip.tx_pending && first_tx_us == 0) first_tx_us = t;
        }
    }

    uint64_t first_tx_us{0};
};

}  // namespace

// The draw chooses the instant and nothing on air moves it: core/timing/README.md has the argument.
TEST_CASE("rf: the burst keys at its drawn instant, on a held channel as on a quiet one") {
    models::Sx1262 chip;
    parts::Sx1262 radio(chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin);
    platform::host::Clock clock;
    bus::Queue<events::RfEvent, 8> events;
    platform::host::Rf rf(radio, clock, events);
    REQUIRE(rf.begin() == Status::Ok);

    const uint8_t frame[protocol::AdslPacket::kTxBytes] = {0x72, 0x4B};
    ports::RfPlan plan{};
    plan.mode = ports::RfMode::RxMband;
    plan.freq_hz = timing::kMband0Hz;
    plan.start_us = 450000;
    plan.end_us = 795000;
    plan.tx = frame;
    plan.tx_len = sizeof(frame);
    plan.tx_at_us = 600000;

    chip.rssi_dbm = -50;  // a neighbour holding the channel, 55 dB over the floor
    REQUIRE(rf.arm(plan) == Status::Ok);
    uint32_t keyed_at_ms = 0;
    for (uint32_t t = 450; t <= 800; t += 5) {
        clock.set_millis(t);
        rf.service(t);
        if (chip.tx_pending && keyed_at_ms == 0) keyed_at_ms = t;
    }
    CHECK(keyed_at_ms == 600);

    // The same dwell one second later on a quiet channel keys at the same instant.
    uint8_t sent[protocol::kTxPayloadChipBytes];
    uint8_t sent_len = 0;
    REQUIRE(chip.take_tx(sent, sent_len));
    chip.rssi_dbm = simulator::Air::kNoiseFloorDbm;
    plan.start_us += 1000000;
    plan.end_us += 1000000;
    plan.tx_at_us += 1000000;
    REQUIRE(rf.arm(plan) == Status::Ok);
    keyed_at_ms = 0;
    for (uint32_t t = 1450; t <= 1800; t += 5) {
        clock.set_millis(t);
        rf.service(t);
        if (chip.tx_pending && keyed_at_ms == 0) keyed_at_ms = t;
    }
    CHECK(keyed_at_ms == 1600);
    CHECK(radio.mode() == parts::RadioMode::Tx);
    CHECK(chip.tx_pending);
    // Two hundred milliseconds in Tx with no TxDone, and no firmware timer
    // anywhere: see the case below for why that is the right answer.
    CHECK(chip.tx_timeout_ticks != 0);
}

// A7, decided and written down: there is no second, firmware-side transmit
// watchdog in the executor or in the driver's service, and there does not need
// to be one. SetTx carries a timeout of air time plus 25 ms (DS 13.4.1), so the
// hardware unkeys the PA on its own and no firmware timer can be faster; poll()
// turns that into a counted recovery. A backstop could only run on the thread
// that owns the radio, which is the same loop that already reads the IRQ, so it
// would fire strictly after the chip does. The one gap it might have covered - a
// burst issued so late that the dwell closes before the chip's timeout expires -
// is closed by the next dwell, which is standby-bracketed (DS 13.1) and unkeys
// the transmitter on its way to the new channel.
TEST_CASE("rf: the SetTx timeout is the transmit watchdog, and the next dwell is the backstop") {
    models::Sx1262 chip;
    parts::Sx1262 radio(chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin);
    REQUIRE(radio.begin() == Status::Ok);
    parts::RadioConfig cfg{};
    cfg.freq_hz = timing::kMband0Hz;
    cfg.sync = protocol::kSharedSync;
    cfg.sync_bits = protocol::kSharedSyncBits;
    cfg.payload_bytes = protocol::kRxChipBytes;
    REQUIRE(radio.configure_radio(cfg) == Status::Ok);
    REQUIRE(radio.start_receive() == Status::Ok);

    uint8_t frame[protocol::kAdslFrameBytes] = {0};
    REQUIRE(radio.transmit(frame, sizeof(frame)) == Status::Ok);
    // The chip is holding a timer that expires well inside one 400 ms dwell.
    const uint32_t timeout_ms = chip.tx_timeout_ticks * parts::sx::kTimeoutStepNs / 1000000u;
    CHECK(timeout_ms > 0);
    CHECK(timeout_ms < timing::kSlot0End - timing::kSlot0Start);

    // And a TxDone that never came and a timeout nobody polled still leave the
    // transmitter off before the next channel opens.
    cfg.freq_hz = timing::kMband1Hz;
    REQUIRE(radio.configure_radio(cfg) == Status::Ok);
    CHECK(chip.standby);
    CHECK_FALSE(chip.receiving);
    CHECK(radio.mode() == parts::RadioMode::Standby);
    CHECK(chip.fault == models::Sx1262::Fault::None);
}

// A8. Capability::Rf used to be ORed in unconditionally, and bring-up only
// proved BUSY went low - which an empty footprint with a pull-down does too. A
// radio that is not there then boots as a radio that is simply never hearing
// anything, which is the hardest fault on this board to diagnose in the field.
TEST_CASE("rf: a radio that will not answer over SPI is an absent capability, not a quiet one") {
    platform::host::Platform fitted_platform;
    go::Product<platform::host::Platform> fitted{fitted_platform};
    CHECK(ports::has(fitted.capabilities(), ports::Capability::Rf));
    CHECK(fitted.setup() == Status::Ok);
    CHECK(fitted.flyable());

    platform::host::Platform dead_platform;
    dead_platform.chips().radio.miso_dead = true;
    go::Product<platform::host::Platform> dead{dead_platform};
    CHECK_FALSE(ports::has(dead.capabilities(), ports::Capability::Rf));
    // The radio is required, so the loop refuses to fly - and the self-test page
    // that names the part is painted before anything refuses.
    CHECK(dead.setup() == Status::Down);
    CHECK_FALSE(dead.flyable());
    CHECK(ports::has(ports::missing(dead.capabilities(), go::kRequired), ports::Capability::Rf));
}

// B3. The phase used to be whatever the board sampled at the top of the pass,
// combined with a clock read at the bottom of it. Everything the service list
// does in between - parsing a GNSS burst, painting a panel - moved the transmit
// instant that far late, on top of the 5 ms the jitter guard already allows for.
TEST_CASE("rf: the transmit instant is measured from the latched edge, not from a stale phase") {
    for (uint32_t lag_ms : {0u, 4u, 9u}) {
        Pass pass;
        REQUIRE(pass.begin() == Status::Ok);

        // A real edge is not on a millisecond boundary either.
        // This second's draw is 535 ms: inside the slot, after the pass that arms it.
        const uint64_t edge_us = 2000000;  // an even second, so the draw is the lower channel's
        pass.poll_clock(edge_us + 460450);
        const uint64_t at_service_us = edge_us + 460450 + lag_ms * 1000;
        const uint32_t now_ms = static_cast<uint32_t>(at_service_us / 1000);
        pass.services_at(at_service_us);

        const timing::SlotPlan plan = timing::Scheduler{}.plan(500, pass.state.clock);
        const timing::Transmitter::Attempt wanted =
            pass.radio_service.transmitter().attempt(plan, 2, now_ms, true, 0);
        REQUIRE(wanted.go);
        const uint64_t wanted_us = edge_us + static_cast<uint64_t>(wanted.at_ms) * 1000;

        pass.executor_until(edge_us + 800000);
        REQUIRE(pass.first_tx_us != 0);
        const int64_t error_us =
            static_cast<int64_t>(pass.first_tx_us) - static_cast<int64_t>(wanted_us);
        CAPTURE(lag_ms);
        CAPTURE(error_us);
        CHECK(error_us >= 0);
        CHECK(error_us < timing::kJitterGuardMs * 1000);
        // And in fact inside the millisecond the PPS surface is quantised to,
        // whatever the pass costs: the edge is an instant, not a phase.
        CHECK(error_us < 1000);
    }
}

// A burst drained after the edge and before the sentence naming it read a second early.
TEST_CASE("rf: a burst at the end of a second is dated in the second it happened in") {
    Pass pass;
    REQUIRE(pass.begin() == Status::Ok);

    const uint64_t edge_us = 12'000'000;
    pass.state.own.utc = 45296;
    pass.state.clock.pps_locked = true;
    pass.state.clock.utc_s = 45296;
    pass.state.clock.utc_edge_us = edge_us;
    pass.state.clock.pps_edge_us = edge_us;

    events::RfEvent burst{};
    burst.type = events::RfEventType::CrcError;
    burst.band = model::Band::M;
    burst.freq_hz = timing::kMband1Hz;
    burst.at_us = edge_us + 954'000;
    burst.rssi_dbm = -101;
    burst.rssi_valid = true;
    pass.bus.rf.push(burst);

    // The next edge is latched, and the receiver has not named its second yet.
    timing::carry_utc_to_edge(pass.state.clock, edge_us + 1'000'000);
    const uint64_t drained_us = edge_us + 1'010'000;
    pass.platform.clock().set_micros(drained_us);
    pass.traffic_service.tick(static_cast<uint32_t>(drained_us / 1000));

    REQUIRE(pass.state.radio_log.count() == 1);
    const radio::Entry& row = pass.state.radio_log.newest(0);
    CHECK(row.at_s == 45296);
    CHECK(row.into_ms == 954);
    CHECK(row.channel == 1);
    // The level a refused burst arrived at is what separates noise from a neighbour.
    CHECK(row.rssi_valid);
    CHECK(row.rssi_dbm == -101);
}

// E1. A site where the carrier never read clear used to be a device that went silent.
TEST_CASE("rf: a jammed site transmits at its instant, and reports the floor it measured") {
    Pass pass;
    REQUIRE(pass.begin() == Status::Ok);
    CHECK(pass.radio_service.noise_floor().dbm() == timing::NoiseFloor::kSeedDbm);

    pass.chip.rssi_dbm = -50;  // a neighbour sitting on the channel, 55 dB over the floor
    int keyed_at_ms = 0;
    for (uint64_t t = 0; t <= 2900000; t += 10000) {
        pass.whole_pass(t);
        if (pass.chip.tx_pending && keyed_at_ms == 0)
            keyed_at_ms = static_cast<int>(t / 1000 % 1000);
    }

    // The PA keyed inside the direct slot. Nothing here completes the burst: the
    // world that takes it off the antenna is simulator::Air, and this harness has none.
    CHECK(pass.chip.saw_cmd(parts::sx::kSetTx));
    CHECK(timing::Scheduler::in_direct_slot(keyed_at_ms));
    CHECK(pass.state.rf.timing_stats.refused() == 0);
    // The floor is a measurement, and a site this loud has walked it off the seed.
    CHECK(pass.radio_service.noise_floor().samples() > 3);
    CHECK(pass.radio_service.noise_floor().dbm() > timing::NoiseFloor::kSeedDbm);
}

// A read at one instant used to license a burst on top of a neighbour it did not land on.
TEST_CASE("rf: the floor the service publishes is a window of reads, and moves no instant") {
    Pass pass;
    REQUIRE(pass.begin() == Status::Ok);

    // A quiet channel with a neighbour occupying one ninth of every window.
    const int8_t window[timing::ChannelLevel::kSamples] = {-115, -115, -115, -115, -45,
                                                           -115, -115, -115, -115};
    pass.chip.set_rssi_sequence(window, timing::ChannelLevel::kSamples);

    int keyed_at_ms = 0;
    for (uint64_t t = 0; t <= 2900000; t += 10000) {
        pass.whole_pass(t);
        if (pass.chip.tx_pending && keyed_at_ms == 0)
            keyed_at_ms = static_cast<int>(t / 1000 % 1000);
    }
    // The floor the service averages is the window's figure, not the quiet read.
    CHECK(pass.radio_service.noise_floor().dbm() > window[0] + 20);

    // The same channel read only at its quiet instant keys the PA at the same instant.
    Pass single;
    REQUIRE(single.begin() == Status::Ok);
    single.chip.rssi_dbm = window[0];
    int single_keyed_at_ms = 0;
    for (uint64_t t = 0; t <= 2900000; t += 10000) {
        single.whole_pass(t);
        if (single.chip.tx_pending && single_keyed_at_ms == 0)
            single_keyed_at_ms = static_cast<int>(t / 1000 % 1000);
    }
    CHECK(single.radio_service.noise_floor().dbm() < timing::NoiseFloor::kSeedDbm);
    CHECK(pass.radio_service.noise_floor().dbm() > single.radio_service.noise_floor().dbm() + 10);
    CHECK(single_keyed_at_ms == keyed_at_ms);
}

// The 30 s no-RX reinitialisation lived in parts::Sx1262::service(), which only
// the host executor ever called: on silicon hardware/platform/zephyr/rf.h had an
// empty service() body, so the radio health watchdog this pins did not exist on
// the device at all. It belongs to whichever thread owns the radio, because
// reinitialising from the service list would put a second writer on the SPI bus
// while a dwell is using it.
TEST_CASE("rf: a receiver that hears nothing is reinitialised by the executor that owns it") {
    models::Sx1262 chip;
    parts::Sx1262 radio(chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin);
    platform::host::Clock clock;
    bus::Queue<events::RfEvent, 8> events;
    platform::host::Rf rf(radio, clock, events);
    REQUIRE(rf.begin() == Status::Ok);

    ports::RfPlan plan{};
    plan.mode = ports::RfMode::RxMband;
    plan.freq_hz = timing::kMband0Hz;
    plan.start_us = 0;
    plan.end_us = 400000;
    REQUIRE(rf.arm(plan) == Status::Ok);

    const uint32_t deadline_ms = runtime::kRadioNoRxReinitMs;
    for (uint32_t t = 0; t < deadline_ms; t += 10) {
        clock.set_millis(t);
        rf.service(t);
    }
    CHECK(radio.reinit_count() == 0);
    for (uint32_t t = deadline_ms; t <= deadline_ms + 100; t += 10) {
        clock.set_millis(t);
        rf.service(t);
    }
    CHECK(radio.reinit_count() == 1);
    CHECK(radio.mode() == parts::RadioMode::Rx);

    // A frame that arrives restarts the rope rather than shortening it.
    uint8_t burst[protocol::kAdslFrameBytes] = {0};
    chip.queue_rx(burst, sizeof(burst));
    for (uint32_t t = deadline_ms + 100; t <= 2 * deadline_ms; t += 10) {
        clock.set_millis(t);
        rf.service(t);
    }
    CHECK(radio.reinit_count() == 1);
}

// Every dwell restarts the receiver, and restarting it once zeroed the rope: no unit ever got here.
TEST_CASE("rf: a receiver re-armed every dwell is still reinitialised when it hears nothing") {
    models::Sx1262 chip;
    parts::Sx1262 radio(chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin);
    platform::host::Clock clock;
    bus::Queue<events::RfEvent, 8> events;
    platform::host::Rf rf(radio, clock, events);
    REQUIRE(rf.begin() == Status::Ok);

    for (uint32_t t = 0; t <= runtime::kRadioNoRxReinitMs + 1000; t += 10) {
        if (t % 400 == 0) {
            ports::RfPlan plan{};
            plan.mode = ports::RfMode::RxMband;
            plan.freq_hz = timing::kMband0Hz;
            plan.start_us = static_cast<uint64_t>(t) * 1000;
            plan.end_us = plan.start_us + 390000;
            REQUIRE(rf.arm(plan) == Status::Ok);
        }
        clock.set_millis(t);
        rf.service(t);
    }
    CHECK(radio.reinit_count() == 1);
    CHECK(rf.armed_count() > runtime::kRadioNoRxReinitMs / 400);
}

// A reinit that failed left the radio out of Rx, where the rope stops counting: dead for good.
TEST_CASE("rf: a reinitialisation that failed is tried again a rope later") {
    models::Sx1262 chip;
    parts::Sx1262 radio(chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin);
    platform::host::Clock clock;
    bus::Queue<events::RfEvent, 8> events;
    platform::host::Rf rf(radio, clock, events);
    REQUIRE(rf.begin() == Status::Ok);

    ports::RfPlan plan{};
    plan.mode = ports::RfMode::RxMband;
    plan.freq_hz = timing::kMband0Hz;
    plan.start_us = 0;
    plan.end_us = 400000;
    REQUIRE(rf.arm(plan) == Status::Ok);
    for (uint32_t t = 0; t <= 400; t += 10) {
        clock.set_millis(t);
        rf.service(t);
    }
    REQUIRE(radio.mode() == parts::RadioMode::Rx);

    // It resets to standby, then its reads come back as nothing: out of Rx, and failed.
    chip.miso_dead = true;
    for (uint32_t t = 410; t <= 2 * runtime::kRadioNoRxReinitMs + 1000; t += 10) {
        clock.set_millis(t);
        rf.service(t);
    }
    CHECK(radio.reinit_count() >= 2);
}

// A radio half configured may still sit on the last dwell's channel, and it keyed there.
TEST_CASE("rf: a dwell whose radio would not configure keys nothing, and says so") {
    models::Sx1262 chip;
    parts::Sx1262 radio(chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin);
    platform::host::Clock clock;
    bus::Queue<events::RfEvent, 8> events;
    platform::host::Rf rf(radio, clock, events);
    REQUIRE(rf.begin() == Status::Ok);

    const uint8_t frame[protocol::AdslPacket::kTxBytes] = {0x72, 0x4B};
    ports::RfPlan plan{};
    plan.mode = ports::RfMode::RxMband;
    plan.freq_hz = timing::kMband1Hz;
    plan.start_us = 400000;
    plan.end_us = 799000;
    plan.tx = frame;
    plan.tx_len = sizeof(frame);
    plan.tx_at_us = 600000;
    REQUIRE(rf.arm(plan) == Status::Ok);

    chip.busy_stuck = true;
    for (uint32_t t = 390; t <= 410; t += 10) {
        clock.set_millis(t);
        rf.service(t);
    }
    chip.busy_stuck = false;
    for (uint32_t t = 420; t <= 800; t += 10) {
        clock.set_millis(t);
        rf.service(t);
    }

    CHECK_FALSE(chip.saw_cmd(parts::sx::kSetTx));
    events::RfEvent e{};
    bool missed = false;
    while (events.pop(e)) missed = missed || e.type == events::RfEventType::Missed;
    CHECK(missed);
}
