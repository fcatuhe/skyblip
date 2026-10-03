// The sensor hub on the board: probed, booted while the device flies, then read.
#include <string>

#include "boards/lilygo/t_echo_plus/board.h"
#include "core/bus/bus.h"
#include "core/bus/state.h"
#include "core/dfu/update.h"
#include "core/events/sensor.h"
#include "core/util/sha256.h"
#include "doctest/doctest.h"
#include "hardware/parts/bhi260/image_store.h"
#include "hardware/platform/host/platform.h"
#include "runtime/tasks.h"

using namespace skyblip;

namespace {

using Board = boards::TEchoPlus<platform::host::Platform>;

uint32_t run(platform::host::Platform& platform, Board& board, bus::State& state, uint32_t from_ms,
             uint32_t for_ms) {
    uint32_t t = from_ms;
    for (; t < from_ms + for_ms; t += runtime::kServiceStepMs) {
        platform.clock().set_millis(t);
        board.poll(state, t);
    }
    return t;
}

bool last_sample(bus::Bus& bus, events::AccelSample& out) {
    bool any = false;
    events::AccelSample sample{};
    while (bus.accel.pop(sample)) {
        out = sample;
        any = true;
    }
    return any;
}

}  // namespace

TEST_CASE("board: a sensor hub that answers and names itself is an inclinometer") {
    platform::host::Platform platform;
    bus::Bus bus;
    Board board{platform, bus};

    CHECK(ports::has(board.capabilities(), ports::Capability::Inclinometer));
    CHECK(board.inventory().has_i2c_address(boards::t_echo_plus::kImuAddress));
}

TEST_CASE("board: an address that answers and is not a BHI260 is no inclinometer") {
    platform::host::Platform platform;
    platform.chips().imu.product_id = 0x00;
    bus::Bus bus;
    Board board{platform, bus};

    CHECK_FALSE(ports::has(board.capabilities(), ports::Capability::Inclinometer));
    CHECK(board.inventory().has_i2c_address(boards::t_echo_plus::kImuAddress));
}

TEST_CASE("board: a plain T-Echo has nothing at 0x28 and no inclinometer") {
    platform::host::Platform platform;
    platform.i2c_bus().answer(boards::t_echo_plus::kImuAddress, false);
    bus::Bus bus;
    Board board{platform, bus};

    CHECK_FALSE(ports::has(board.capabilities(), ports::Capability::Inclinometer));
    CHECK_FALSE(board.inventory().has_i2c_address(boards::t_echo_plus::kImuAddress));
}

// The chip is a quarter turn from the case and face down with it: +X is up, +Y right, +Z forward.
TEST_CASE("board: the chip's axes are turned into the case's before anything reads them") {
    platform::host::Platform platform;
    platform.chips().imu.set_acceleration(990, -150, -20);
    bus::Bus bus;
    Board board{platform, bus};
    bus::State state;

    run(platform, board, state, 0, 2000);

    events::AccelSample sample{};
    REQUIRE(last_sample(bus, sample));
    CHECK(sample.right_mg == doctest::Approx(-150).epsilon(0.02));
    CHECK(sample.up_mg == doctest::Approx(990).epsilon(0.02));
    CHECK(sample.aft_mg == doctest::Approx(20).epsilon(0.1));
}

TEST_CASE("board: the hub is booted from the loop, and reports once it runs") {
    platform::host::Platform platform;
    platform.chips().imu.set_acceleration(990, -150, -20);
    bus::Bus bus;
    Board board{platform, bus};
    bus::State state;

    const uint32_t t = run(platform, board, state, 0, 2000);
    CHECK(board.imu().stage() == parts::Bhi260::Stage::Running);
    CHECK(platform.chips().imu.booted);

    events::AccelSample sample{};
    REQUIRE(last_sample(bus, sample));
    CHECK(sample.right_mg == doctest::Approx(-150).epsilon(0.02));
    CHECK(sample.up_mg == doctest::Approx(990).epsilon(0.02));
    CHECK(sample.aft_mg == doctest::Approx(20).epsilon(0.1));
    CHECK(sample.at_ms < t);
}

TEST_CASE("board: nothing is asked of a bus with no hub on it") {
    platform::host::Platform platform;
    platform.i2c_bus().answer(boards::t_echo_plus::kImuAddress, false);
    bus::Bus bus;
    Board board{platform, bus};
    bus::State state;

    run(platform, board, state, 0, 2000);

    CHECK(board.imu().stage() == parts::Bhi260::Stage::Absent);
    events::AccelSample sample{};
    CHECK_FALSE(last_sample(bus, sample));
}

namespace {

using Holding = parts::Bhi260ImageStore::Holding;

void store_on_flash(platform::host::Platform& platform, const uint8_t* image, size_t bytes) {
    parts::Bhi260ImageStore store{platform.imu_flash()};
    REQUIRE(store.begin_write(ConstByteSpan(image, bytes), Sha256::of(image, bytes)));
    while (store.writing()) store.step();
    REQUIRE(store.holding() == Holding::Held);
}

std::string reported(const bus::State& state) {
    char text[dfu::kHubImageTextCap];
    dfu::format_hub_image(state.imu.image, text, sizeof(text));
    return text;
}

std::string hex_prefix(const Sha256::Digest& digest) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < dfu::HubImageReport::kDigestBytes; i++) {
        out += kHex[digest[i] >> 4];
        out += kHex[digest[i] & 0x0F];
    }
    return out;
}

}  // namespace

TEST_CASE("board: a full image keeps a copy of the hub's image on flash once it is confirmed") {
    platform::host::Platform platform;
    platform.dfu().image_confirmed = false;
    bus::Bus bus;
    Board board{platform, bus};
    bus::State state;

    uint32_t t = run(platform, board, state, 0, 2000);
    CHECK(board.imu().stage() == parts::Bhi260::Stage::Running);
    CHECK(reported(state) == "missing");
    CHECK(platform.imu_flash().erases == 0);

    platform.dfu().image_confirmed = true;
    run(platform, board, state, t, 2000);
    CHECK(board.imu_store().holds(platform.imu_firmware_digest()));
    CHECK(reported(state) == hex_prefix(platform.imu_firmware_digest()));
    CHECK(state.imu.bootable);
}

TEST_CASE("board: a slim image boots the hub from the copy on flash") {
    platform::host::Platform platform;
    const ConstByteSpan carried = platform.imu_firmware();
    platform.set_imu_firmware_linked(false);
    store_on_flash(platform, carried.data(), carried.size());
    bus::Bus bus;
    Board board{platform, bus};
    bus::State state;

    run(platform, board, state, 0, 2000);
    CHECK(board.imu().stage() == parts::Bhi260::Stage::Running);
    CHECK(platform.chips().imu.booted);
    CHECK(platform.chips().imu.uploaded() == carried.size());
    CHECK(state.imu.bootable);
    CHECK(std::string(state.imu.fault).empty());
}

TEST_CASE("board: a slim image on an empty partition leaves the hub idle and says why") {
    platform::host::Platform platform;
    platform.set_imu_firmware_linked(false);
    bus::Bus bus;
    Board board{platform, bus};
    bus::State state;

    run(platform, board, state, 0, 2000);
    CHECK(board.imu().stage() == parts::Bhi260::Stage::Idle);
    CHECK_FALSE(platform.chips().imu.booted);
    CHECK(std::string(state.imu.stage) == "IDLE");
    CHECK(std::string(state.imu.fault) == "NOBLOB");
    CHECK_FALSE(state.imu.bootable);
    CHECK(board.imu_without_image());
    CHECK(reported(state) == "missing");
    CHECK(platform.imu_flash().erases == 0);
}

TEST_CASE("board: a slim image refuses a copy it was not built with") {
    platform::host::Platform platform;
    platform.set_imu_firmware_linked(false);
    const uint8_t other[] = {0x2B, 0x66, 0x00, 0x00, 0x99, 0x88, 0x77, 0x66};
    store_on_flash(platform, other, sizeof(other));
    bus::Bus bus;
    Board board{platform, bus};
    bus::State state;

    run(platform, board, state, 0, 2000);
    CHECK_FALSE(platform.chips().imu.booted);
    CHECK(std::string(state.imu.fault) == "NOBLOB");
    CHECK(reported(state) == hex_prefix(Sha256::of(other, sizeof(other))));
}

TEST_CASE("board: a full image replaces a copy that is not its own") {
    platform::host::Platform platform;
    const uint8_t other[] = {0x2B, 0x66, 0x00, 0x00, 0x99, 0x88, 0x77, 0x66};
    store_on_flash(platform, other, sizeof(other));
    bus::Bus bus;
    Board board{platform, bus};
    bus::State state;

    run(platform, board, state, 0, 2000);
    CHECK(board.imu_store().holds(platform.imu_firmware_digest()));
}

TEST_CASE("board: a plain T-Echo reports no hub and keeps nothing on flash") {
    platform::host::Platform platform;
    platform.i2c_bus().answer(boards::t_echo_plus::kImuAddress, false);
    bus::Bus bus;
    Board board{platform, bus};
    bus::State state;

    run(platform, board, state, 0, 2000);
    CHECK(reported(state) == "none");
    CHECK(state.imu.bootable);
    CHECK_FALSE(board.imu_without_image());
    CHECK(platform.imu_flash().erases == 0);
    CHECK(platform.imu_flash().reads == 0);
}

TEST_CASE("board: the copy waits for a flash window, as the log does") {
    platform::host::Platform platform;
    bus::Bus bus;
    Board board{platform, bus};
    bus::State state;
    state.rf.plan.tx_allowed = true;

    run(platform, board, state, 0, 2000);
    CHECK(platform.imu_flash().erases == 0);
    CHECK(reported(state) == "writing");
}

TEST_CASE("board: a hub that answers but whose flash does not is reported unreadable") {
    platform::host::Platform platform;
    platform.set_imu_firmware_linked(false);
    platform.imu_flash().set_present(false);
    bus::Bus bus;
    Board board{platform, bus};
    bus::State state;

    run(platform, board, state, 0, 2000);
    CHECK(reported(state) == "unreadable");
    CHECK(std::string(state.imu.fault) == "NOBLOB");
}
