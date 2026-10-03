// The station log: newest first, and nothing kept that a screen could not show.
#include <initializer_list>

#include "core/model/aircraft.h"
#include "core/radio/log.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {

radio::Entry heard(uint32_t addr, uint32_t at_s) {
    radio::Entry e{};
    e.event = radio::Event::Received;
    e.source = model::Source::AdslDirect;
    e.addr = addr;
    e.at_s = at_s;
    e.rssi_dbm = -87;
    e.rssi_valid = true;
    e.utc = true;
    return e;
}

}  // namespace

TEST_CASE("radio log: an empty log has nothing to read") {
    radio::Log log;
    CHECK(log.count() == 0);
}

TEST_CASE("radio log: the burst that just happened is the one at the top") {
    radio::Log log;
    log.record(heard(0xAAAAAA, 10));
    log.record(heard(0xBBBBBB, 11));
    log.record(heard(0xCCCCCC, 12));

    REQUIRE(log.count() == 3);
    CHECK(log.newest(0).addr == 0xCCCCCC);
    CHECK(log.newest(1).addr == 0xBBBBBB);
    CHECK(log.newest(2).addr == 0xAAAAAA);
}

// What falls off the bottom of the screen is gone: the page is a tape, not a history.
TEST_CASE("radio log: past capacity the oldest burst is the one dropped") {
    radio::Log log;
    for (int i = 0; i < radio::Log::kCapacity + 5; i++)
        log.record(heard(static_cast<uint32_t>(i), static_cast<uint32_t>(i)));

    CHECK(log.count() == radio::Log::kCapacity);
    CHECK(log.newest(0).addr == radio::Log::kCapacity + 4);
    CHECK(log.newest(radio::Log::kCapacity - 1).addr == 5);
}

TEST_CASE("radio log: every outcome a burst can have is one it keeps") {
    radio::Log log;
    for (radio::Event event :
         {radio::Event::Transmitted, radio::Event::Lost, radio::Event::Held, radio::Event::Unarmed,
          radio::Event::Received, radio::Event::BadCrc, radio::Event::Undecoded,
          radio::Event::Unsupported, radio::Event::Unattempted}) {
        radio::Entry e{};
        e.event = event;
        log.record(e);
        CHECK(log.newest(0).event == event);
    }
}

TEST_CASE("radio log: a burst is dated by the phase it landed at, not the pass that drained it") {
    const events::Stamp stamp = events::stamp_of(12'462'000, 12'000'000, true, 45296);

    CHECK(stamp.phase_valid);
    CHECK(stamp.at_s == 45296);
    CHECK(stamp.into_ms == 462);
}

// Slot 1 closes 200 ms into the next second, so its tail is drained under a second the air had not.
TEST_CASE("radio log: a burst before the edge belongs to the second its dwell opened in") {
    const events::Stamp stamp = events::stamp_of(11'950'000, 12'000'000, true, 45296);

    CHECK(stamp.phase_valid);
    CHECK(stamp.at_s == 45295);
    CHECK(stamp.into_ms == 950);
}

// Without an edge to measure from, a phase is arithmetic on an instant nothing anchored.
TEST_CASE("radio log: an unlocked clock reports the second and refuses the phase") {
    const events::Stamp stamp = events::stamp_of(12'462'000, 12'000'000, false, 45296);

    CHECK_FALSE(stamp.phase_valid);
    CHECK(stamp.at_s == 45296);
    CHECK(stamp.into_ms == 0);
}

TEST_CASE("radio log: an instant the latched edge cannot reach carries no phase") {
    const events::Stamp stamp = events::stamp_of(12'000'000, 20'000'000, true, 45296);

    CHECK_FALSE(stamp.phase_valid);
    CHECK(stamp.at_s == 45296);
}

// Keying to completion: §C.2's M-band burst is 4800 us of it, the pass that collected it the rest.
TEST_CASE("radio log: a sent burst measures from the instant it was aimed at to the one reported") {
    CHECK(radio::tx_span_of(12'004'800, 12'000'000) == 4800);
    CHECK(radio::tx_span_of(12'000'000, 12'000'000) == 0);
}

// A span too wide to bound is one this row must not report as a smaller one.
TEST_CASE("radio log: a span past the column saturates rather than wrapping") {
    CHECK(radio::tx_span_of(13'000'000, 12'000'000) == radio::kTxSpanLimitUs);
}

// A report cannot precede the instant the burst was keyed at.
TEST_CASE("radio log: a completion before its own deadline measures nothing") {
    CHECK(radio::tx_span_of(11'999'000, 12'000'000) == 0);
}

TEST_CASE("radio log: a cleared log holds nothing and starts over") {
    radio::Log log;
    log.record(heard(0xAAAAAA, 10));
    log.clear();
    REQUIRE(log.count() == 0);

    log.record(heard(0xBBBBBB, 11));
    REQUIRE(log.count() == 1);
    CHECK(log.newest(0).addr == 0xBBBBBB);
}

// Staging that ends after the instant is what keys a burst late, so the sign is the finding.
TEST_CASE("radio log: a stage margin is how early the burst was staged, negative when it overran") {
    CHECK(radio::tx_stage_margin_of(11'999'420, 12'000'000) == 580);
    CHECK(radio::tx_stage_margin_of(12'000'150, 12'000'000) == -150);
}
