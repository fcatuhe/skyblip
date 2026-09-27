// The TCXO on DIO3: it starts once, and no retune, burst or hop pays its 5 ms start again.
#include <algorithm>

#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/parts/sx1262/sx1262.h"

using namespace skyblip;
using namespace skyblip::parts;

namespace {

Sx1262 make(models::Sx1262& f) { return Sx1262(f, f, f, f.busy_pin, f.reset_pin, f.dio1_pin); }

RadioConfig dwell(uint32_t freq_hz) {
    RadioConfig cfg{};
    cfg.freq_hz = freq_hz;
    cfg.sync = protocol::kSharedSync;
    cfg.sync_bits = protocol::kSharedSyncBits;
    cfg.payload_bytes = protocol::kRxChipBytes;
    return cfg;
}

void command(models::Sx1262& chip, uint8_t opcode, uint8_t param) {
    const uint8_t bytes[2] = {opcode, param};
    chip.select(true);
    chip.transfer(bytes, nullptr, sizeof(bytes));
    chip.select(false);
}

void burst(Sx1262& r, models::Sx1262& chip) {
    uint8_t frame[protocol::kAdslFrameBytes] = {0};
    REQUIRE(r.transmit(frame, sizeof(frame)) == Status::Ok);
    chip.signal_tx_done();
    uint8_t rx[protocol::kRxChipBytes];
    REQUIRE(r.poll(rx, sizeof(rx)).type == RadioEventType::TxDone);
}

}  // namespace

TEST_CASE("radio: the TCXO starts when the receiver is first armed, and not again while it runs") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(dwell(868200000)) == Status::Ok);
    CHECK(chip.tcxo_starts == 0);

    REQUIRE(r.start_receive() == Status::Ok);
    CHECK(chip.tcxo_running);
    CHECK(chip.tcxo_starts == 1);
    REQUIRE(r.start_receive() == Status::Ok);
    CHECK(chip.tcxo_starts == 1);
}

// Every dwell change went through STDBY_RC, which unpowers DIO3, and the part then
// waited the whole TCXO start before it listened: about 5 ms deaf, three times a second.
TEST_CASE("radio: a retune keeps the TCXO running, so the next dwell listens without its start") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(dwell(868200000)) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    REQUIRE(chip.tcxo_starts == 1);

    RadioConfig uplink = dwell(869525000);
    uplink.bitrate = protocol::kUplinkChipRateBps;
    REQUIRE(r.configure_radio(uplink) == Status::Ok);
    CHECK(chip.receiving);
    CHECK((chip.freq_hz + 500) / 1000 == 869525);
    CHECK(chip.tcxo_starts == 1);
    CHECK(chip.fault == models::Sx1262::Fault::None);
}

TEST_CASE("radio: after a burst the part falls back to STDBY_XOSC and re-arms on a running TCXO") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(dwell(868200000)) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    REQUIRE(chip.tcxo_starts == 1);

    burst(r, chip);
    CHECK(chip.standby);
    CHECK(chip.tcxo_running);
    REQUIRE(r.start_receive() == Status::Ok);
    CHECK(chip.tcxo_starts == 1);
}

// The two M-band dwells are one modulation on two channels, and rewriting the
// whole modem to move 200 kHz was most of what the hop cost.
TEST_CASE("radio: a hop between the M-band channels writes the frequency and nothing else") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(dwell(868200000)) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    chip.cmds_seen.clear();

    REQUIRE(r.configure_radio(dwell(868400000)) == Status::Ok);
    CHECK(chip.receiving);
    CHECK((chip.freq_hz + 500) / 1000 == 868400);
    CHECK(chip.saw_cmd(sx::kSetRfFrequency));
    CHECK_FALSE(chip.saw_cmd(sx::kSetModulationParams));
    CHECK_FALSE(chip.saw_cmd(sx::kSetPacketParams));
    CHECK_FALSE(chip.saw_cmd(sx::kWriteRegister));
    CHECK(chip.tcxo_starts == 1);
    CHECK(chip.fault == models::Sx1262::Fault::None);
}

TEST_CASE("radio: a part told to fall back to STDBY_XOSC keeps its TCXO through a burst") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    command(chip, sx::kSetRxTxFallbackMode, sx::kFallbackStdbyXosc);
    REQUIRE(r.configure_radio(dwell(868200000)) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    REQUIRE(chip.tcxo_starts == 1);

    burst(r, chip);
    CHECK(chip.standby);
    CHECK(chip.tcxo_running);
    REQUIRE(r.start_receive() == Status::Ok);
    CHECK(chip.tcxo_starts == 1);
    CHECK(chip.fault == models::Sx1262::Fault::None);
}

TEST_CASE("radio: re-arming the dwell the radio is already tuned for keeps the TCXO running") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(dwell(868200000)) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    const auto standbys = std::count(chip.cmds_seen.begin(), chip.cmds_seen.end(), sx::kSetStandby);

    REQUIRE(r.configure_radio(dwell(868200000)) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    CHECK(std::count(chip.cmds_seen.begin(), chip.cmds_seen.end(), sx::kSetStandby) == standbys);
    CHECK(chip.receiving);
    CHECK(chip.tcxo_starts == 1);
    CHECK(chip.fault == models::Sx1262::Fault::None);
}

TEST_CASE("radio: another sync word on the same channel is another dwell, and is written") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(dwell(868200000)) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);

    RadioConfig uplink = dwell(868200000);
    uplink.sync = protocol::kUplinkSync;
    uplink.sync_bits = protocol::kUplinkSyncBits;
    REQUIRE(r.configure_radio(uplink) == Status::Ok);
    CHECK(chip.sync_bits == protocol::kUplinkSyncBits);
    CHECK(std::equal(chip.sync, chip.sync + protocol::kUplinkSyncBits / 8, protocol::kUplinkSync));
    CHECK(chip.tcxo_starts == 1);
}

TEST_CASE("radio: after a sleep the same dwell is written again, not trusted to the warm start") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(dwell(868200000)) == Status::Ok);
    r.sleep();
    REQUIRE(r.wake() == Status::Ok);
    const auto tunes =
        std::count(chip.cmds_seen.begin(), chip.cmds_seen.end(), sx::kSetRfFrequency);

    REQUIRE(r.configure_radio(dwell(868200000)) == Status::Ok);
    CHECK(std::count(chip.cmds_seen.begin(), chip.cmds_seen.end(), sx::kSetRfFrequency) ==
          tunes + 1);
}
