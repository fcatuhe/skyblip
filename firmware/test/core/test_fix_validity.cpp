// I, row "Fix age as validity". The bug being pinned: `fix.valid` used to mean
// "the last RMC said A", which survives the receiver going silent, losing its
// GGA, reporting a date from 1980 or teleporting across a country. SoftRF
// requires GGA and RMC both present with every age inside 3500 ms
// (oss/SoftRF-lyusupov .../src/driver/GNSS.cpp:1487-1503); moshe-braner adds the
// jump gate (.../SoftRF.ino:517-523). A fix that fails any of it is not a fix,
// and each refusal is named so a self-test page can say which one.
#include <cstring>

#include "core/gnss/nmea.h"
#include "core/gnss/validity.h"
#include "doctest/doctest.h"

using namespace skyblip::gnss;

namespace {
// A receiver burst as it arrives: RMC then GGA, both fed through the real
// parser, both stamped with the instant they landed.
struct Burst {
    NmeaParser parser;
    FixValidity validity;

    void feed(const char* line, uint32_t at_ms) {
        const int characters = static_cast<int>(std::strlen(line));
        REQUIRE(parser.parse_line(line, characters));
        validity.observe(parser.solution(), parser.last_sentence(), at_ms);
    }
};

constexpr const char* kRmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230825,003.1,W*6B";
constexpr const char* kGga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
}

TEST_CASE("fix validity: one sentence is never a fix, both of them are") {
    Burst burst;
    CHECK(burst.validity.evaluate(0) == FixReject::MissingRmc);

    burst.feed(kRmc, 1000);
    // RMC alone carries position, speed, track and the date. It carries no
    // altitude and no quality, and this device transmits altitude.
    CHECK(burst.validity.evaluate(1000) == FixReject::MissingGga);

    burst.feed(kGga, 1000);
    CHECK(burst.validity.valid(1000));
}

// 3500 ms is SoftRF's NMEA_EXP_TIME: three missed solutions, liveness, not the freshness rule.
TEST_CASE("fix validity: a receiver that stops talking stops having a fix") {
    Burst burst;
    burst.feed(kRmc, 1000);
    burst.feed(kGga, 1000);

    CHECK(burst.validity.valid(1000 + kSentenceMaxAgeMs));        // the last instant it holds
    CHECK_FALSE(burst.validity.valid(1001 + kSentenceMaxAgeMs));  // and the first it does not
    CHECK(burst.validity.evaluate(5000) == FixReject::Stale);

    // Half a burst is as stale as none of it: a receiver that keeps sending RMC
    // and has lost its GGA has no altitude worth transmitting, and its position
    // ages out on the sentence that stopped.
    burst.feed(kRmc, 6000);
    CHECK(burst.validity.evaluate(6000) == FixReject::Stale);
    burst.feed(kGga, 6000);
    CHECK(burst.validity.valid(6000));
}

TEST_CASE("fix validity: V means no fix, and so does a GGA quality outside 1 to 5") {
    Burst burst;
    burst.feed(kRmc, 1000);
    burst.feed(kGga, 1000);
    REQUIRE(burst.validity.valid(1000));

    // GGA quality 6 is dead reckoning: the receiver telling us it is guessing.
    // SoftRF accepts GPS through float RTK and nothing else.
    const char* reckoned = "$GPGGA,123519,4807.038,N,01131.000,E,6,08,0.9,545.4,M,46.9,M,,*40";
    burst.feed(reckoned, 1200);
    CHECK(burst.validity.evaluate(1200) == FixReject::NoSolution);

    // DGPS is quality 2 and perfectly good.
    const char* dgps_sentence = "$GPGGA,123519,4807.038,N,01131.000,E,2,08,0.9,545.4,M,46.9,M,,*44";
    burst.feed(dgps_sentence, 1400);
    CHECK(burst.validity.valid(1400));

    const char* void_rmc = "$GPRMC,123523,V,,,,,,,230825,,*3B";
    burst.feed(void_rmc, 1600);
    CHECK(burst.validity.evaluate(1600) == FixReject::NoSolution);
}

// The date is part of the fix, not a decoration: SoftRF calls it crucial and
// refuses the fix without it. Our timestamp code, our flight log session and
// every log record carry it.
TEST_CASE("fix validity: a fix with the MTK 1980 date is not a fix") {
    Burst burst;
    const char* lie = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230380,003.1,W*6F";
    burst.feed(lie, 1000);
    burst.feed(kGga, 1000);
    CHECK(burst.validity.evaluate(1000) == FixReject::NoDate);

    burst.feed(kRmc, 1200);
    CHECK(burst.validity.valid(1200));
}

// moshe-braner's jump gate: 0.15 deg of latitude or 0.25 deg of longitude
// between consecutive solutions is bad data, not flight. 0.15 deg is 16.7 km.
TEST_CASE("fix validity: a position no aircraft could have flown to is refused once") {
    Burst burst;
    burst.feed(kRmc, 1000);
    burst.feed(kGga, 1000);
    REQUIRE(burst.validity.valid(1000));

    // 48.117 N to 51.117 N in one solution: three degrees, 330 km.
    const char* jump = "$GPRMC,123521,A,5107.038,N,01131.000,E,022.4,084.4,230825,003.1,W*68";
    burst.feed(jump, 1200);
    CHECK(burst.validity.evaluate(1200) == FixReject::Jump);

    // One spike costs one fix. The new position becomes the reference, so a
    // receiver that has genuinely moved recovers on the next solution instead of
    // sulking forever against a position we are no longer at.
    const char* after = "$GPRMC,123522,A,5107.038,N,01131.000,E,022.4,084.4,230825,003.1,W*6B";
    burst.feed(after, 1400);
    CHECK(burst.validity.valid(1400));
}

TEST_CASE("fix validity: crossing the antimeridian is flight, not a jump") {
    Burst burst;
    burst.feed(kRmc, 1000);
    burst.feed(kGga, 1000);
    GnssSolution west_edge = burst.parser.solution();
    west_edge.longitude_1e7 = 1799999000;
    burst.validity.observe(west_edge, Sentence::Rmc, 1200);
    GnssSolution east_edge = west_edge;
    east_edge.longitude_1e7 = -1799999000;
    burst.validity.observe(east_edge, Sentence::Rmc, 1400);
    CHECK(burst.validity.valid(1400));
}

TEST_CASE("fix validity: the jump gate is drawn where moshe-braner draws it") {
    // A step just inside the gate is 16.7 km in a second: receiver faults, not implausible flight.
    Burst burst;
    burst.feed(kRmc, 1000);
    burst.feed(kGga, 1000);
    const int32_t base_latitude = burst.parser.solution().latitude_1e7;

    GnssSolution inside = burst.parser.solution();
    inside.latitude_1e7 = base_latitude + kLatitudeJumpCeiling1e7;
    burst.validity.observe(inside, Sentence::Rmc, 1200);
    CHECK(burst.validity.valid(1200));

    GnssSolution outside = inside;
    outside.latitude_1e7 = inside.latitude_1e7 + kLatitudeJumpCeiling1e7 + 1;
    burst.validity.observe(outside, Sentence::Rmc, 1400);
    CHECK(burst.validity.evaluate(1400) == FixReject::Jump);
}

// Switched off in a car, switched on at the airfield. The jump reference is
// dropped with the fix, because a receiver that reacquires somewhere else has
// not jumped: moshe-braner keeps the stale reference and eats one bad fix on
// reacquisition, which is exactly the fix a pilot is watching for.
TEST_CASE("fix validity: losing the fix drops the jump reference") {
    Burst burst;
    burst.feed(kRmc, 1000);
    burst.feed(kGga, 1000);
    REQUIRE(burst.validity.valid(1000));

    const char* lost = "$GPRMC,123523,V,,,,,,,230825,,*3B";
    burst.feed(lost, 1200);
    REQUIRE_FALSE(burst.validity.valid(1200));

    const char* elsewhere = "$GPRMC,123521,A,5107.038,N,01131.000,E,022.4,084.4,230825,003.1,W*68";
    burst.feed(elsewhere, 1400);
    burst.feed(kGga, 1400);
    CHECK(burst.validity.valid(1400));
}

// What support reads. A run of refusals for one cause is one event: "the antenna
// came off twice", not "the antenna came off four thousand times".
TEST_CASE("fix validity: refusals are counted once per cause, not once per poll") {
    Burst burst;
    burst.feed(kRmc, 1000);
    burst.feed(kGga, 1000);
    REQUIRE(burst.validity.check(1000) == FixReject::None);
    CHECK(burst.validity.rejected() == 0);

    for (uint32_t now_ms = 5000; now_ms < 6000; now_ms += 10) burst.validity.check(now_ms);
    CHECK(burst.validity.rejected() == 1);
    CHECK(burst.validity.last_reject() == FixReject::Stale);

    burst.feed(kRmc, 6000);
    burst.feed(kGga, 6000);
    CHECK(burst.validity.check(6000) == FixReject::None);
    CHECK(burst.validity.last_reject() == FixReject::None);

    const char* void_rmc = "$GPRMC,123523,V,,,,,,,230825,,*3B";
    burst.feed(void_rmc, 6200);
    CHECK(burst.validity.check(6200) == FixReject::NoSolution);
    CHECK(burst.validity.rejected() == 2);
}

// M. The 3500 ms liveness bound across the 49.7-day wrap of ports::Clock::milliseconds().
// The failure this would be: a receiver that is talking perfectly well is declared
// STALE for the seven weeks after the wrap, so the device stops transmitting and
// stops logging while its GNSS light says everything is fine.
TEST_CASE("fix validity: the staleness bound spans the 49.7-day wrap") {
    Burst burst;
    const uint32_t before = 0xFFFFFF00u;  // 256 ms short of the wrap
    burst.feed(kRmc, before);
    burst.feed(kGga, before);

    CHECK(burst.validity.valid(before));
    // 100 ms after the wrap: 356 ms old, which is a fix.
    CHECK(burst.validity.valid(100u));
    // The bound still lands exactly 3500 ms after the sentences, counted through
    // zero rather than restarting at it.
    CHECK(burst.validity.valid(before + kSentenceMaxAgeMs));
    CHECK_FALSE(burst.validity.valid(before + kSentenceMaxAgeMs + 1u));
    CHECK(burst.validity.evaluate(before + 5000u) == FixReject::Stale);

    // And a burst that arrives after the wrap revives it, once, not once per poll.
    burst.feed(kRmc, 4000u);
    burst.feed(kGga, 4000u);
    CHECK(burst.validity.check(4000u) == FixReject::None);
    CHECK(burst.validity.rejected() == 0);  // nothing above ever called check()
}
