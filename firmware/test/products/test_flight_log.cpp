// The flight log on the running product: the same board, the same service list,
// the same part models the silicon build uses, over a flash fake that keeps
// NOR's two awkward truths and knows how to die mid-program.
#include <string>
#include <vector>

#include "core/events/link.h"
#include "core/flight/log_record.h"
#include "core/store/sector.h"
#include "doctest/doctest.h"
#include "ports/link.h"
#include "test/support/log_transfer.h"
#include "test/support/product_rig.h"
#include "test/support/rig_moves.h"

using namespace skyblip;

TEST_CASE("flight log: a device parked on the ground writes nothing at all") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    const uint32_t erases_at_boot = rig.platform.log_flash().erases;
    uint32_t t = 0;

    taxi(rig, t, 120);

    CHECK_FALSE(rig.product.flight_log().recording());
    CHECK(rig.product.flight_log().records_written() == 0);
    CHECK(rig.platform.log_flash().writes == 0);
    CHECK(rig.platform.log_flash().erases == erases_at_boot);
}

TEST_CASE("flight log: a takeoff opens a session, a landing closes it, the ground after is quiet") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;

    taxi(rig, t, 40);
    REQUIRE_FALSE(rig.product.flight_log().recording());

    fly(rig, t, 120);
    CHECK(rig.product.flight_log().recording());
    // The session is named for a sample taken before core/flight would commit
    // to the takeoff, so the ground roll is in the file.
    CHECK(rig.product.flight_log().session_id() < Rig::kUtcBase + 45);
    const uint32_t airborne_records = rig.product.flight_log().records_written();
    // Two minutes at a record every four seconds, plus the pre-takeoff ring.
    CHECK(airborne_records >= 30);

    taxi(rig, t, 40);
    CHECK_FALSE(rig.product.flight_log().recording());
    const uint32_t after_landing = rig.product.flight_log().records_written();
    // core/flight needs ten seconds of stillness before it will call it a
    // landing, and those seconds are part of the flight, so they are logged;
    // then one last record marks the end.
    CHECK(after_landing > airborne_records);

    // And then nothing: the ring goes back to being a holding pen, and nothing
    // the writer owed was lost on the way.
    taxi(rig, t, 60);
    CHECK(rig.product.flight_log().records_written() == after_landing);
    CHECK(rig.product.flight_log().records_dropped() == 0);
    CHECK(rig.product.flight_log().sessions_on_flash() == 1);
}

TEST_CASE("flight log: the sector behind the one being filled is erased before it is needed") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    const uint32_t erases_at_boot = rig.platform.log_flash().erases;
    uint32_t t = 0;

    taxi(rig, t, 20);
    fly(rig, t, 60);
    REQUIRE(rig.product.flight_log().recording());

    // One erase for the sector the records go into, one for the spare behind it.
    CHECK(rig.platform.log_flash().erases == erases_at_boot + 2);
    const uint32_t spare = rig.product.flight_log().ring().sector() + 1;
    uint8_t label[store::kSectorHeaderBytes];
    REQUIRE(is_ok(rig.platform.log_flash().read(spare * platform::host::FlashRegion::kSectorBytes,
                                                label, sizeof(label))));
    // Erased and unlabelled, so a power cut leaves it a free sector.
    CHECK(store::erased(label, sizeof(label)));
}

TEST_CASE("flight log: a committed record survives the cell dying, and the torn one is not a fix") {
    Rig flight;
    REQUIRE(flight.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(flight, t, 20);
    fly(flight, t, 80);
    const uint32_t committed = flight.product.flight_log().records_written();
    REQUIRE(committed > 8);

    // The cell gives out nine bytes into the next record. Everything before it
    // is on the part; that record is half a record.
    flight.platform.log_flash().cut_power_after(9);
    fly(flight, t, 20);
    REQUIRE(flight.platform.log_flash().dead());
    CHECK(flight.product.flight_log().records_written() == committed);

    // A reboot is a new device handed the same bytes.
    Rig rebooted;
    rebooted.platform.log_flash().restore(flight.platform.log_flash().bytes());
    REQUIRE(rebooted.setup() == Status::Ok);

    uint32_t rt = 0;
    taxi(rebooted, rt, 20);
    CHECK(field(list_count(rebooted, rt), "sessions") == "1");

    const std::string listed = list_session(rebooted, rt, 0);
    // Every record that was committed is still offered, and the half-written one
    // is not counted: it is never read back as a position.
    CHECK(field(listed, "records") == std::to_string(committed));
    // The flight stops where the power did, and the tablet is told so rather
    // than shown a truncated flight as a complete one.
    CHECK(field(listed, "closed") == "false");
    CHECK(field(listed, "session") == std::to_string(flight.product.flight_log().session_id()));
}

TEST_CASE("flight log: the next flight after a power cut does not overwrite the last one") {
    Rig flight;
    REQUIRE(flight.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(flight, t, 20);
    fly(flight, t, 60);
    const uint32_t first_session = flight.product.flight_log().session_id();
    const uint32_t committed = flight.product.flight_log().records_written();

    Rig rebooted;
    rebooted.platform.log_flash().restore(flight.platform.log_flash().bytes());
    REQUIRE(rebooted.setup() == Status::Ok);
    // The device came back up ten minutes later, which is what makes the second
    // session a second session and not a continuation of the first.
    rebooted.utc_offset_s = 600;
    uint32_t rt = 0;
    taxi(rebooted, rt, 20);
    fly(rebooted, rt, 60);
    taxi(rebooted, rt, 40);
    CHECK(rebooted.product.flight_log().session_id() != first_session);

    CHECK(field(list_count(rebooted, rt), "sessions") == "2");

    bool found_first = false;
    for (uint32_t i = 0; i < 2; i++) {
        const std::string line = list_session(rebooted, rt, i);
        REQUIRE(line.find("\"cmd\":\"session\"") != std::string::npos);
        if (field(line, "session") != std::to_string(first_session)) continue;
        found_first = true;
        // The flight that was on the part before the reboot is untouched by the
        // one flown after it.
        CHECK(field(line, "records") == std::to_string(committed));
    }
    CHECK(found_first);
}

TEST_CASE("flight log: the write frontier is found from the labels, not by reading the partition") {
    Rig flight;
    REQUIRE(flight.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(flight, t, 20);
    fly(flight, t, 60);

    Rig rebooted;
    rebooted.platform.log_flash().restore(flight.platform.log_flash().bytes());
    REQUIRE(rebooted.setup() == Status::Ok);

    const uint32_t partition =
        platform::host::FlashRegion::kSectorBytes * platform::host::FlashRegion::kSectorCount;
    // One 16-byte label per sector: 5280 bytes of a 1.29 MB partition, which is
    // the difference between a boot that is instant and a boot that is a
    // quarter of a second of SPI.
    CHECK(rebooted.product.flight_log().recovery_bytes_read() ==
          store::kSectorHeaderBytes * platform::host::FlashRegion::kSectorCount);
    CHECK(rebooted.product.flight_log().recovery_bytes_read() * 200 < partition);
}
