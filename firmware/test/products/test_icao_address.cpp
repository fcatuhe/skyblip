// ADS-L F.2.3 as flown: a set own ICAO is the address on air and on glass, not the schedule.
#include <cstdint>
#include <cstring>
#include <vector>

#include "core/settings/address.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/identity.h"
#include "products/skyblip_go/settings.h"
#include "protocols/adsl/packet.h"
#include "simulator/simulator.h"
#include "test/support/glass_text.h"
#include "test/support/product_rig.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {

constexpr uint32_t kOwnIcao = 0x3C6586;
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

TEST_CASE("rf: an own ICAO puts Traffic, Status and name under table 5 and that address") {
    simulator::Simulator simulator;
    fly(simulator);
    simulator.product().settings().own_icao = kOwnIcao;
    run_on(simulator, past_settling(simulator), 21000);

    const std::vector<Sent> sent = sent_on(simulator.world().air());
    CHECK(count_of(sent, adsl::Packet::kPayloadTypePosition) >= 19);
    CHECK(count_of(sent, adsl::Packet::kPayloadTypeStatus) >= 2);
    CHECK(count_of(sent, adsl::Packet::kPayloadTypeOgnDiagnostics) >= 2);
    CHECK(all_from(sent, {adsl::kAddressTableIcao, kOwnIcao}));
}

TEST_CASE("rf: with no own ICAO, Traffic, Status and name go under table 58 and the serial") {
    simulator::Simulator simulator;
    fly(simulator);
    REQUIRE(simulator.product().settings().own_icao == go::kOwnIcaoUnset);
    run_on(simulator, past_settling(simulator), 21000);

    const std::vector<Sent> sent = sent_on(simulator.world().air());
    CHECK(count_of(sent, adsl::Packet::kPayloadTypeStatus) >= 2);
    CHECK(count_of(sent, adsl::Packet::kPayloadTypeOgnDiagnostics) >= 2);
    CHECK(all_from(sent, {settings::kSkyblipAddressMappingTable, kSerialAddress}));
}

// F.2.3 asks for the setting to be revertible: clearing it is the whole revert, from the next
// burst.
TEST_CASE("rf: clearing the own ICAO puts table 58 and the serial back on the next burst") {
    simulator::Simulator simulator;
    fly(simulator);
    simulator.product().settings().own_icao = kOwnIcao;
    const uint32_t from_ms = past_settling(simulator);
    run_on(simulator, from_ms, 10000);
    REQUIRE(all_from(sent_on(simulator.world().air()), {adsl::kAddressTableIcao, kOwnIcao}));

    simulator.world().air().clear();
    simulator.product().settings().own_icao = go::kOwnIcaoUnset;
    run_on(simulator, from_ms + 10000 + simulator::Simulator::kStepMs, 21000);

    const std::vector<Sent> sent = sent_on(simulator.world().air());
    CHECK(count_of(sent, adsl::Packet::kPayloadTypePosition) >= 19);
    CHECK(count_of(sent, adsl::Packet::kPayloadTypeStatus) >= 1);
    CHECK(all_from(sent, {settings::kSkyblipAddressMappingTable, kSerialAddress}));
}

// The cycle start and the drawn instants key on the serial, so the address alone changes.
TEST_CASE("rf: setting an own ICAO mid-flight moves no burst in time or channel") {
    simulator::Simulator unset;
    simulator::Simulator set;
    fly(unset);
    fly(set);
    const uint32_t from_ms = past_settling(unset);
    REQUIRE(past_settling(set) == from_ms);
    run_on(unset, from_ms, 10000);
    run_on(set, from_ms, 10000);
    set.product().settings().own_icao = kOwnIcao;
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
        if (after[i].sender == Sender{adsl::kAddressTableIcao, kOwnIcao}) moved_to_icao++;
    }
    CHECK(moved_to_icao >= 20);
}

// F.2.3 overrides the address on air, not the activation behind it: no serial, no burst.
TEST_CASE("rf: an own ICAO does not stand in for activation, a unit with no serial stays silent") {
    Rig rig(platform::host::Platform::kFullyFitted, std::nullopt);
    REQUIRE(rig.setup() == Status::Ok);
    rig.settings().own_icao = kOwnIcao;
    uint32_t now_ms = 0;
    rig.seconds(now_ms, 20, 0, 300);

    CHECK_FALSE(rig.state().activation.activated());
    CHECK_FALSE(settings::on_air(rig.state().activation.address(), kOwnIcao).address);
    CHECK(rig.product.radio().arm_count() == 0);
    CHECK(rig.state().air.tx_ok == 0);
    CHECK(rig.product.radio().transmitter().sent_count() == 0);
}

// Nearby is the page open when somebody asks who you are, and they now see the ICAO address.
TEST_CASE("product: nearby and status name the address on air, the own ICAO once it is set") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.settings().own_icao = kOwnIcao;
    uint32_t now_ms = 100;
    for (const go::Page page : {go::Page::Nearby, go::Page::Status}) {
        rig.show(now_ms, page);
        rig.run(now_ms, now_ms + 4000);
        now_ms += 4000;
        CHECK(reads_in(rig.product.screen().framebuffer(), "3C6586", 0, 0, go::kGlassWidth,
                       go::kGlassHeight, 2));
        CHECK_FALSE(reads_in(rig.product.screen().framebuffer(), "005AFE", 0, 0, go::kGlassWidth,
                             go::kGlassHeight, 2));
    }
}
