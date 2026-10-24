// The status page pins the units a pilot reads first, what each sensor and the receiver say
// about themselves, and the widest position on earth still fitting its row.
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/status.h"
#include "test/support/glass_text.h"

using namespace skyblip::go;
using skyblip::reads_in;

TEST_CASE("status page: every value reads in the aeronautical unit first, then SI") {
    // 1500 m = 4921 ft, 20 m/s = 39 kt, +2.0 m/s =
    // +394 fpm. Rendering is 5x7 glyphs, so the check is on the row's ink: the
    // dual-unit row is wider than a single-unit one would be.
    Glass both;
    StatusSnapshot snapshot;
    snapshot.fix_valid = true;
    snapshot.utc_valid = true;
    snapshot.satellites = 9;
    snapshot.altitude_mm = 1500000;
    snapshot.speed_mm_s = 20000;
    snapshot.climb_mm_s = 2000;
    snapshot.track_cdeg = 9000;
    draw_status(both, snapshot);

    // The barometric rows only exist when a barometer answered: the pressure it
    // reads, and pressure altitude on the 1013.25 standard setting.
    Glass with_barometer;
    StatusSnapshot barometer_read = snapshot;
    barometer_read.barometer_valid = true;
    barometer_read.pressure_mpa = 84556000;
    barometer_read.altitude_standard_mm = 1500000;
    draw_status(with_barometer, barometer_read);
    CHECK(with_barometer.count_black() > both.count_black());
}

TEST_CASE("status page: the barometer row reads what the sensor resolves") {
    Glass canvas;
    StatusSnapshot snapshot;
    snapshot.barometer_valid = true;
    snapshot.pressure_mpa = 101325253;  // the BME280's own tenths of a pascal
    snapshot.climb_mm_s = -1234;        // -243 fpm, a rate the 0.125 m/s of ADS-L cannot hold
    draw_status(canvas, snapshot);

    CHECK(reads_in(canvas, "1013.253", 0, 85, 200, 105));
    CHECK(reads_in(canvas, "-243", 0, 149, 200, 169));
    CHECK(reads_in(canvas, "-1.234", 0, 149, 200, 169));
}

TEST_CASE("status page: every reading is printed to the resolution it is held in") {
    Glass canvas;
    StatusSnapshot snapshot;
    snapshot.fix_valid = true;
    snapshot.barometer_valid = true;
    snapshot.track_cdeg = 9025;
    snapshot.altitude_mm = 1234500;  // GGA carries tenths of a metre
    snapshot.altitude_standard_mm = 1500123;
    snapshot.speed_mm_s = 12345;  // 44.442 km/h
    snapshot.climb_mm_s = 1234;
    snapshot.battery_valid = true;
    snapshot.battery_mv = 4123;
    draw_status(canvas, snapshot);

    CHECK(reads_in(canvas, "090.25", 0, 55, 100, 70));
    CHECK(reads_in(canvas, "1234.5", 0, 101, 200, 121));
    CHECK(reads_in(canvas, "1500.12", 0, 117, 200, 137));
    CHECK(reads_in(canvas, "44.44", 0, 133, 200, 153));
    CHECK(reads_in(canvas, "+1.234", 0, 149, 200, 169));
    CHECK(reads_in(canvas, "4.123", 0, 165, 200, 185));
}

TEST_CASE("status page: the IMU field reads the hub's own bring-up word and the ball it feeds") {
    StatusSnapshot snapshot;
    snapshot.imu_stage = "RUN";
    snapshot.slip_valid = true;
    snapshot.slip_mg = -120;
    Glass running;
    draw_status(running, snapshot);
    CHECK(reads_in(running, "IMU RUN -120mg", 0, 55, 200, 70));

    // A hub that answered and never delivered a sample: the word without a ball.
    StatusSnapshot quiet = snapshot;
    quiet.slip_valid = false;
    Glass no_data;
    draw_status(no_data, quiet);
    CHECK(reads_in(no_data, "IMU RUN B0", 0, 55, 200, 70));
    CHECK_FALSE(reads_in(no_data, "mg", 0, 55, 200, 70));

    StatusSnapshot absent;
    Glass none;
    draw_status(none, absent);
    CHECK(reads_in(none, "IMU NONE", 0, 55, 200, 70));
}

// A silent hub and one whose FIFO nobody can step through are two different faults.
TEST_CASE("status page: a hub reporting nothing says what the FIFO gave it") {
    StatusSnapshot snapshot;
    snapshot.imu_stage = "RUN";
    snapshot.imu_fifo_bytes = 4096;
    Glass feeding;
    draw_status(feeding, snapshot);
    CHECK(reads_in(feeding, "IMU RUN B99", 0, 55, 200, 70));

    StatusSnapshot stuck = snapshot;
    stuck.imu_unparsed = 3;
    stuck.imu_error = 0x1A;
    Glass unparsed;
    draw_status(unparsed, stuck);
    CHECK(reads_in(unparsed, "IMU RUN U3 E1A", 0, 55, 200, 70));
    CHECK(reads_in(unparsed, "TRUE", 0, 55, 200, 70));
}

// The bench run that asked for this: a hub up, 18 bytes drained and no ball.
TEST_CASE("status page: a silent hub reports the last thing it said about itself") {
    StatusSnapshot snapshot;
    snapshot.imu_stage = "RUN";
    snapshot.imu_fifo_bytes = 18;
    snapshot.imu_meta_event = 12;
    Glass overflowed;
    draw_status(overflowed, snapshot);
    CHECK(reads_in(overflowed, "IMU RUN B18 M12", 0, 55, 200, 70));

    StatusSnapshot errored = snapshot;
    errored.imu_errored_sensor = 4;
    errored.imu_sensor_error = 0x23;
    Glass refused;
    draw_status(refused, errored);
    CHECK(reads_in(refused, "IMU RUN SE4:23", 0, 55, 200, 70));
    CHECK(reads_in(refused, "TRUE", 0, 55, 200, 70));
}

// Only the interrupt register separates a hub producing nothing from a FIFO nobody read.
TEST_CASE("status page: an announced hub shows what the hub says it is holding") {
    StatusSnapshot snapshot;
    snapshot.imu_stage = "RUN";
    snapshot.imu_fifo_bytes = 18;
    snapshot.imu_meta_event = 16;
    snapshot.imu_interrupt = 0x18;
    Glass holding;
    draw_status(holding, snapshot);
    CHECK(reads_in(holding, "IMU RUN B18 I18", 0, 55, 200, 70));
    CHECK(reads_in(holding, "TRUE", 0, 55, 200, 70));

    StatusSnapshot quiet = snapshot;
    quiet.imu_interrupt = 0;
    Glass empty;
    draw_status(empty, quiet);
    CHECK(reads_in(empty, "IMU RUN B18 I00", 0, 55, 200, 70));
}

TEST_CASE("status page: a bring-up that named the fault stops on the stage that found it") {
    StatusSnapshot snapshot;
    snapshot.imu_stage = "CONF";
    snapshot.imu_fault = "NOSENS";
    Glass absent;
    draw_status(absent, snapshot);
    CHECK(reads_in(absent, "IMU CONF NOSENS", 0, 55, 200, 70));
    CHECK(reads_in(absent, "TRUE", 0, 55, 200, 70));

    StatusSnapshot refused = snapshot;
    refused.imu_fault = "NOCFG";
    Glass dropped;
    draw_status(dropped, refused);
    CHECK(reads_in(dropped, "IMU CONF NOCFG", 0, 55, 200, 70));
}

TEST_CASE("status page: the widest sensor error still leaves the track's datum readable") {
    StatusSnapshot snapshot;
    snapshot.imu_stage = "RUN";
    snapshot.imu_errored_sensor = 255;
    snapshot.imu_sensor_error = 0xFF;
    Glass canvas;
    draw_status(canvas, snapshot);

    CHECK(reads_in(canvas, "IMU RUN SE255:FF", 0, 55, 200, 70));
    CHECK(reads_in(canvas, "TRUE", 0, 55, 200, 70));
}

TEST_CASE("status page: the widest IMU failure still leaves the track's datum readable") {
    StatusSnapshot snapshot;
    snapshot.imu_stage = "LOAD";
    snapshot.imu_fault = "TIMEOUT";
    Glass canvas;
    draw_status(canvas, snapshot);

    CHECK(reads_in(canvas, "IMU LOAD TIMEOUT", 0, 55, 200, 70));
    CHECK(reads_in(canvas, "TRUE", 0, 55, 200, 70));
}

TEST_CASE(
    "status page: the battery row states the voltage and the charge, and on the cable only CHG") {
    StatusSnapshot snapshot;
    snapshot.battery_valid = true;
    snapshot.battery_mv = 4000;
    snapshot.battery_percent = 89;

    Glass resting;
    draw_status(resting, snapshot);
    CHECK(reads_in(resting, "89", 0, 160, 200, 194));

    // On the cable the divider reads the USB rail, so the row shows no number at all.
    StatusSnapshot on_charge;
    on_charge.charging = true;
    Glass charging;
    draw_status(charging, on_charge);
    CHECK(reads_in(charging, "CHG", 0, 160, 200, 194));
    CHECK(reads_in(charging, "--", 0, 160, 200, 194));
    CHECK_FALSE(reads_in(charging, "no sensor", 0, 160, 200, 194));

    // A board with no divider fitted says so rather than reading empty.
    StatusSnapshot absent;
    Glass no_sensor;
    draw_status(no_sensor, absent);
    CHECK(reads_in(no_sensor, "no sensor", 0, 160, 200, 194));

    // The cutoff monitor's warning, on the page: a cell at 3.45 V is low and
    // says so, and the cable takes the word off.
    StatusSnapshot low_cell = snapshot;
    low_cell.battery_mv = 3450;
    low_cell.battery_percent = 12;
    low_cell.battery_low = true;
    Glass low;
    draw_status(low, low_cell);
    StatusSnapshot quiet_cell = low_cell;
    quiet_cell.battery_low = false;
    Glass quiet;
    draw_status(quiet, quiet_cell);
    CHECK(low.count_black() > quiet.count_black());
    CHECK(reads_in(low, "LOW", 0, 160, 200, 194));

    StatusSnapshot on_cable = on_charge;
    on_cable.battery_low = true;
    Glass cable;
    draw_status(cable, on_cable);
    CHECK_FALSE(reads_in(cable, "LOW", 0, 160, 200, 194));

    // The row is the last one on the panel: it has to fit inside it.
    for (int y = 194; y < Glass::kHeight; y++)
        for (int x = 0; x < Glass::kWidth; x++) CHECK_FALSE(charging.get_pixel(x, y));
}

TEST_CASE("status page: a receiver with no fix says how far it has got, and for how long") {
    StatusSnapshot snapshot;
    snapshot.satellites = 9;
    snapshot.stage = skyblip::gnss::Stage::Blind;
    snapshot.stage_s = 48;
    Glass searching;
    draw_status(searching, snapshot);

    CHECK(reads_in(searching, "BLIND 0:48", 0, 24, 140, 40));
    CHECK_FALSE(reads_in(searching, "SAT", 0, 24, 140, 40));
    CHECK_FALSE(reads_in(searching, "3D", 0, 24, 140, 40));

    // A date decoded is a satellite read, which is the rung a bare NO FIX hid.
    StatusSnapshot timed = snapshot;
    timed.stage = skyblip::gnss::Stage::Solving;
    timed.stage_s = 80;
    Glass reading;
    draw_status(reading, timed);
    CHECK(reads_in(reading, "SOLVING 1:20", 0, 24, 140, 40));

    StatusSnapshot silent = snapshot;
    silent.stage = skyblip::gnss::Stage::Silent;
    silent.stage_s = 5;
    Glass quiet;
    draw_status(quiet, silent);
    CHECK(reads_in(quiet, "SILENT 0:05", 0, 24, 140, 40));

    StatusSnapshot fixed = snapshot;
    fixed.fix_valid = true;
    fixed.fix_mode = skyblip::gnss::kFixMode3D;
    Glass solved;
    draw_status(solved, fixed);
    CHECK(reads_in(solved, "3D", 0, 24, 140, 40));
    CHECK(reads_in(solved, "9 SAT", 0, 24, 140, 40));
    CHECK_FALSE(reads_in(solved, "BLIND", 0, 24, 140, 40));
}

// The receiver's own GSA answer where it gave one, the satellite count where it did not.
TEST_CASE("status page: a solution without height reads 2D") {
    StatusSnapshot snapshot;
    snapshot.fix_valid = true;
    snapshot.satellites = 9;
    snapshot.fix_mode = skyblip::gnss::kFixMode2D;
    Glass two_dimensional;
    draw_status(two_dimensional, snapshot);
    CHECK(reads_in(two_dimensional, "2D", 0, 24, 140, 40));

    StatusSnapshot unreported = snapshot;
    unreported.fix_mode = 0;
    unreported.satellites = 3;
    Glass inferred;
    draw_status(inferred, unreported);
    CHECK(reads_in(inferred, "2D", 0, 24, 140, 40));

    unreported.satellites = 9;
    Glass plenty;
    draw_status(plenty, unreported);
    CHECK(reads_in(plenty, "3D", 0, 24, 140, 40));
}

// The page used to report PPS lock, a pin a pilot cannot act on.
TEST_CASE("status page: the page reports whether own-ship is transmitting, not the PPS pin") {
    StatusSnapshot snapshot;
    snapshot.fix_valid = true;
    snapshot.satellites = 9;
    Glass silent;
    draw_status(silent, snapshot);
    CHECK(reads_in(silent, "TX OFF", 100, 65, 200, 85));
    CHECK_FALSE(reads_in(silent, "PPS", 0, 65, 200, 85));

    StatusSnapshot seen = snapshot;
    seen.transmitting = true;
    Glass on_air;
    draw_status(on_air, seen);
    CHECK(reads_in(on_air, "TX ON", 100, 65, 200, 85));
    CHECK_FALSE(reads_in(on_air, "TX OFF", 100, 65, 200, 85));
}

// B4. ADS-L carries no callsign, so the setting has exactly one job: telling
// three devices on a bench apart. It shares the header with the identity that
// does go on the air.
TEST_CASE("status page: the callsign shares the header with the address, and never crowds it") {
    StatusSnapshot snapshot;
    snapshot.address = skyblip::settings::address_of(0xED12);

    Glass bare;
    draw_status(bare, snapshot);

    StatusSnapshot named = snapshot;
    named.callsign = "D-KXYZ";
    Glass with_name;
    draw_status(with_name, named);
    CHECK(with_name.count_black() > bare.count_black());

    // The address is drawn at scale 2 from the left margin; the name is
    // right-aligned on the same row. Neither may touch the other or the rule
    // under them.
    StatusSnapshot widest = snapshot;
    widest.callsign = "123456789";
    Glass full;
    draw_status(full, widest);
    for (int y = 3; y < 21; y++)
        for (int x = 116; x < 128; x++) CHECK_FALSE(full.get_pixel(x, y));
    for (int y = 3; y < 21; y++) CHECK_FALSE(full.get_pixel(Glass::kWidth - 1, y));

    // An empty callsign is a header with nothing extra on it, not a blank box.
    StatusSnapshot empty = snapshot;
    empty.callsign = "";
    Glass none;
    draw_status(none, empty);
    CHECK(none.count_black() == bare.count_black());
}

TEST_CASE("status page: the widest position on earth still fits its row") {
    // -90.0000000 and -180.0000000: eleven and twelve characters, the most the
    // format can produce. The latitude ends on the first column's unit edge and
    // the longitude block is anchored to the margin, so the worst case is where
    // they nearly meet.
    Glass canvas;
    StatusSnapshot snapshot;
    snapshot.fix_valid = true;
    snapshot.latitude_1e7 = -900000000;
    snapshot.longitude_1e7 = -1800000000;
    draw_status(canvas, snapshot);

    const int y0 = 43, y1 = 50;  // the LAT/LON row, one glyph tall
    for (int y = y0; y < y1; y++)
        for (int x = 196; x < Glass::kWidth; x++) CHECK_FALSE(canvas.get_pixel(x, y));

    // At least one blank column between the latitude and the LON block, and the
    // label is not touched either.
    int blank = 0;
    for (int x = 88; x < 106; x++) {
        bool ink = false;
        for (int y = y0; y < y1; y++) ink = ink || canvas.get_pixel(x, y);
        if (!ink) blank++;
    }
    CHECK(blank >= 1);
    for (int y = y0; y < y1; y++) CHECK_FALSE(canvas.get_pixel(23, y));
}
