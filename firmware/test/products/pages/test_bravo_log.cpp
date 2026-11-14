// The page a bench reads for a Bravo, which has no glass: what it runs, holds and hears, and its
// tape.
#include <cstring>

#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/bravo_log.h"
#include "test/support/glass_text.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

Glass page(const BravoLogSnapshot& snapshot) {
    Glass canvas;
    canvas.clear(true);
    draw_bravo_log(canvas, snapshot);
    return canvas;
}

BravoLogEntry folded(uint32_t address, const char* callsign) {
    BravoLogEntry entry{};
    entry.verdict = BravoVerdict::Folded;
    entry.at.at_s = 45296;  // 12:34:56
    entry.at.utc_dated = true;
    entry.address = address;
    entry.address_valid = true;
    entry.pressure_altitude_ft = 35000;
    entry.pressure_altitude_valid = true;
    std::strcpy(entry.callsign, callsign);
    return entry;
}

}  // namespace

TEST_CASE("bravo log page: the head says the tag, the state, the firmware, the cell and the rate") {
    comms::PeerHealth health{};
    std::strcpy(health.firmware, "0.4.1+1702");
    health.battery_percent = 80;
    health.frame_rate_hz = 412;
    BravoLogSnapshot snapshot;
    snapshot.state = comms::BravoState::Live;
    snapshot.tag = 0x3F2A;
    snapshot.health = &health;
    snapshot.tracked = 14;
    snapshot.folded = 1234;
    const Glass canvas = page(snapshot);
    CHECK(reads_in(canvas, "BRAVO 3F2A", 0, 0, 200, 12));
    CHECK(reads_in(canvas, "LIVE", 100, 0, 200, 12));
    CHECK(reads_in(canvas, "FW 0.4.1+1702", 0, 10, 200, 22));
    CHECK(reads_in(canvas, "BAT 80%", 100, 10, 200, 22));
    CHECK(reads_in(canvas, "1090 412/S", 0, 20, 200, 32));
    CHECK(reads_in(canvas, "B 14 IN 1234", 80, 20, 200, 32));
    CHECK(reads_in(canvas, "NO FRAME FROM BRAVO", 0, 40, 200, 60));

    health.charging = true;
    health.frame_rate_hz.reset();
    const Glass charging = page(snapshot);
    CHECK(reads_in(charging, "BAT CHG", 100, 10, 200, 22));
    CHECK(reads_in(charging, "1090 OFF", 0, 20, 200, 32));
}

TEST_CASE("bravo log page: an unpaired Go or a lost Bravo says so, and draws no reading") {
    BravoLogSnapshot unpaired;
    CHECK(reads_in(page(unpaired), "NOT PAIRED", 100, 0, 200, 12));
    CHECK_FALSE(reads_in(page(unpaired), "FW", 0, 0, 200, 40));

    BravoLogSnapshot lost;
    lost.state = comms::BravoState::LinkDown;
    CHECK(reads_in(page(lost), "LINK DOWN", 100, 0, 200, 12));
    CHECK_FALSE(reads_in(page(lost), "FW", 0, 0, 200, 40));
}

TEST_CASE("bravo log page: a row is the stamp, the verdict, the address and the name or level") {
    BravoLog log;
    log.record(folded(0x3C4A12, ""));
    log.record(folded(0x4CA2D1, "DLH4AB"));
    BravoLogEntry refused{};
    refused.verdict = BravoVerdict::Refused;
    refused.at.at_s = 12;
    log.record(refused);
    BravoLogSnapshot snapshot;
    snapshot.state = comms::BravoState::Live;
    snapshot.log = &log;
    const Glass canvas = page(snapshot);
    CHECK(reads_in(canvas, "T+12", 0, 36, 200, 46));
    CHECK(reads_in(canvas, "BAD", 0, 36, 200, 46));
    CHECK(reads_in(canvas, "12:34:56", 0, 46, 80, 56));
    CHECK(reads_in(canvas, "ADS-B", 70, 46, 120, 56));
    CHECK(reads_in(canvas, "4CA2D1", 100, 46, 160, 56));
    CHECK(reads_in(canvas, "DLH4AB", 120, 46, 200, 56));
    CHECK(reads_in(canvas, "3C4A12", 0, 56, 200, 66));
    CHECK(reads_in(canvas, "FL350", 120, 56, 200, 66));
}
