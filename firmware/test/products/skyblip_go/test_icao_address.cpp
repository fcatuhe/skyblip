// ADS-L F.2.3 as flown: a set aircraft address is the address on air and on glass, not the
// schedule.
#include <cstdint>
#include <cstring>
#include <vector>

#include "core/settings/address.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/identity.h"
#include "products/skyblip_go/settings.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl_edge/registration.h"
#include "simulator/simulator.h"
#include "test/support/glass_text.h"
#include "test/support/product_rig.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {

constexpr uint32_t kAircraftAddress = 0x3C6586;
constexpr uint32_t kSerialAddress = settings::address_of(platform::host::Identity::kSerial);

struct Sender {
    uint8_t table;
    uint32_t address;

    bool operator==(const Sender& other) const {
        return table == other.table && address == other.address;
    }
};

struct Sent {
    uint64_t at_us;
    uint32_t frequency_hz;
    uint8_t payload_type;
    Sender sender;
};

std::vector<Sent> sent_on(const simulator::Air& air) {
    std::vector<Sent> sent;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& record = air.record_at(i);
        adsl::Packet packet{};
        if (!packet_of(record, packet)) continue;
        sent.push_back({record.at_us,
                        record.frequency_hz,
                        packet.payload_type,
                        {packet.address_mapping_table(), packet.address()}});
    }
    return sent;
}

void fly(simulator::Simulator& simulator) {
    REQUIRE(simulator.setup() == Status::Ok);
    std::strncpy(simulator.product().settings().callsign, "D-KXYZ", go::kCallsignBytes - 1);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(50);
}

int count_of(const std::vector<Sent>& sent, uint8_t payload_type) {
    int count = 0;
    for (const Sent& burst : sent)
        if (burst.payload_type == payload_type) count++;
    return count;
}

bool all_from(const std::vector<Sent>& sent, const Sender& sender) {
    for (const Sent& burst : sent)
        if (!(burst.sender == sender)) return false;
    return true;
}

}  // namespace

TEST_CASE("rf: an aircraft address puts Traffic, Status and name under table 5 and that address" *
          doctest::test_suite("main")) {
    simulator::Simulator simulator;
    fly(simulator);
    simulator.product().settings().aircraft_address = kAircraftAddress;
    run_on(simulator, past_settling(simulator), 21000);

    const std::vector<Sent> sent = sent_on(simulator.world().air());
    CHECK(count_of(sent, adsl::Packet::kPayloadTypePosition) >= 19);
    CHECK(count_of(sent, adsl::Packet::kPayloadTypeStatus) >= 2);
    CHECK(count_of(sent, adsl_edge::kPayloadTypeOgnDiagnostics) >= 2);
    CHECK(all_from(sent, {adsl::kAddressTableIcao, kAircraftAddress}));
}

TEST_CASE(
    "rf: with no aircraft address, Traffic, Status and name go under table 58 and the serial" *
    doctest::test_suite("main")) {
    simulator::Simulator simulator;
    fly(simulator);
    REQUIRE(simulator.product().settings().aircraft_address == go::kAircraftAddressUnset);
    run_on(simulator, past_settling(simulator), 21000);

    const std::vector<Sent> sent = sent_on(simulator.world().air());
    CHECK(count_of(sent, adsl::Packet::kPayloadTypeStatus) >= 2);
    CHECK(count_of(sent, adsl_edge::kPayloadTypeOgnDiagnostics) >= 2);
    CHECK(all_from(sent, {settings::kSkyblipAddressMappingTable, kSerialAddress}));
}

// F.2.3 asks for the setting to be revertible: clearing it is the whole revert, from the next
// burst.
TEST_CASE("rf: clearing the aircraft address puts table 58 and the serial back on the next burst") {
    simulator::Simulator simulator;
    fly(simulator);
    simulator.product().settings().aircraft_address = kAircraftAddress;
    const uint32_t from_ms = past_settling(simulator);
    run_on(simulator, from_ms, 10000);
    REQUIRE(
        all_from(sent_on(simulator.world().air()), {adsl::kAddressTableIcao, kAircraftAddress}));

    simulator.world().air().clear();
    simulator.product().settings().aircraft_address = go::kAircraftAddressUnset;
    run_on(simulator, from_ms + 10000 + simulator::Simulator::kStepMs, 21000);

    const std::vector<Sent> sent = sent_on(simulator.world().air());
    CHECK(count_of(sent, adsl::Packet::kPayloadTypePosition) >= 19);
    CHECK(count_of(sent, adsl::Packet::kPayloadTypeStatus) >= 1);
    CHECK(all_from(sent, {settings::kSkyblipAddressMappingTable, kSerialAddress}));
}

// The cycle start and the drawn instants key on the serial, so the address alone changes.
TEST_CASE("rf: setting an aircraft address mid-flight moves no burst in time or channel") {
    simulator::Simulator unset;
    simulator::Simulator set;
    fly(unset);
    fly(set);
    const uint32_t from_ms = past_settling(unset);
    REQUIRE(past_settling(set) == from_ms);
    run_on(unset, from_ms, 10000);
    run_on(set, from_ms, 10000);
    set.product().settings().aircraft_address = kAircraftAddress;
    run_on(unset, from_ms + 10000 + simulator::Simulator::kStepMs, 21000);
    run_on(set, from_ms + 10000 + simulator::Simulator::kStepMs, 21000);

    const std::vector<Sent> before = sent_on(unset.world().air());
    const std::vector<Sent> after = sent_on(set.world().air());
    REQUIRE(before.size() == after.size());
    REQUIRE(before.size() > 20);
    int moved_to_icao = 0;
    for (size_t i = 0; i < before.size(); i++) {
        CHECK(before[i].at_us == after[i].at_us);
        CHECK(before[i].frequency_hz == after[i].frequency_hz);
        CHECK(before[i].payload_type == after[i].payload_type);
        if (after[i].sender == Sender{adsl::kAddressTableIcao, kAircraftAddress}) moved_to_icao++;
    }
    CHECK(moved_to_icao >= 20);
}

// F.2.3 overrides the address on air, not the activation behind it: no serial, no burst.
TEST_CASE(
    "rf: an aircraft address does not stand in for activation, a unit with no serial stays "
    "silent") {
    Rig rig(platform::host::Platform::kFullyFitted, std::nullopt);
    REQUIRE(rig.setup() == Status::Ok);
    rig.settings().aircraft_address = kAircraftAddress;
    rig.seconds(20, 0, 300);

    CHECK_FALSE(rig.state().activation.activated());
    CHECK_FALSE(settings::on_air(rig.state().activation.address(), kAircraftAddress).address);
    CHECK(rig.product.radio().arm_count() == 0);
    CHECK(rig.state().air.tx_ok == 0);
    CHECK(rig.product.radio().transmitter().sent_count() == 0);
}

// Status is the page that names this aircraft, and it names the ICAO address once one is set.
TEST_CASE("product: status names the address on air, the aircraft address once it is set" *
          doctest::test_suite("main")) {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.settings().aircraft_address = kAircraftAddress;
    rig.now_ms = 100;
    rig.show(go::Page::Status);
    rig.run(rig.now_ms, rig.now_ms + 4000);
    CHECK(reads_in(rig.product.screen().framebuffer(), "3C6586", 0, 0, go::kGlassWidth,
                   go::kGlassHeight, 2));
    CHECK_FALSE(reads_in(rig.product.screen().framebuffer(), "005AFE", 0, 0, go::kGlassWidth,
                         go::kGlassHeight, 2));
}
