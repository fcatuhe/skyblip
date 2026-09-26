// The TCXO on DIO3: which modes keep it running, and which re-arms pay its 5 ms start.
#include <algorithm>

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

TEST_CASE("radio: a retune through STDBY_RC stops the TCXO, and the receiver starts it again") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(dwell(868200000)) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    REQUIRE(chip.tcxo_starts == 1);

    REQUIRE(r.configure_radio(dwell(868400000)) == Status::Ok);
    CHECK(chip.receiving);
    CHECK(chip.tcxo_starts == 2);
}

TEST_CASE("radio: after a burst the part falls back to STDBY_RC and re-arms on a cold TCXO") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(dwell(868200000)) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    REQUIRE(chip.tcxo_starts == 1);

    burst(r, chip);
    CHECK(chip.standby);
    CHECK_FALSE(chip.tcxo_running);
    REQUIRE(r.start_receive() == Status::Ok);
    CHECK(chip.tcxo_starts == 2);
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
