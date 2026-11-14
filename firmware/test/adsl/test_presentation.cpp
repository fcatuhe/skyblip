// ADS-L 4 SRD-860 issue 2 Subpart F: the ADS-L header, who a packet is from and what it carries.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>

#include "core/comms/link_registry.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/callsign.h"
#include "core/settings/address.h"
#include "core/util/cbor.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/identity.h"
#include "products/skyblip_go/settings.h"
#include "simulator/simulator.h"
#include "test/support/config_frame.h"
#include "test/support/settings_link.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {
int fewest_of_each_payload_from(const simulator::Air& air, uint8_t table, uint32_t address) {
    int traffic = 0, status = 0, named = 0;
    for (int i = 0; i < air.record_count(); i++) {
        adsl::Packet packet{};
        if (!packet_of(air.record_at(i), packet)) continue;
        if (packet.address_mapping_table() != table || packet.address() != address) return 0;
        if (packet.payload_type == adsl::Packet::kPayloadTypePosition) traffic++;
        if (packet.payload_type == adsl::Packet::kPayloadTypeStatus) status++;
        if (packet.payload_type == adsl::Packet::kPayloadTypeOgnDiagnostics) named++;
    }
    return std::min(traffic, std::min(status, named));
}

adsl::Packet traffic_packet(uint32_t address = 0x123456, uint8_t table = 6) {
    model::Ownship own{};
    own.fix_valid = true;
    own.utc_valid = true;
    own.latitude_1e7 = 481234500;
    own.longitude_1e7 = 81234500;
    own.altitude_hae_mm = 1234000;
    own.speed_mm_s = 45000;  // 180 quarter-m/s on the wire
    own.hdop_e2 = 90;
    own.flight_state = 2;
    adsl::Packet packet{};
    protocol::from_own(packet, own, address, table, 4);
    return packet;
}
}  // namespace

// Payload 66 is the entry F.2.1 assigns to OGN and leaves outside its own scope.
TEST_CASE("ADS-L.4.SRD860.F.2.1: payload 66 carries the registration ADS-L has no field for") {
    adsl::Packet packet{};
    protocol::from_own_callsign(packet, 0x123456, 58, "D-KXYZ");

    CHECK(packet.payload_type == 0x42);
    CHECK(packet.payload_type == 66);
    CHECK(packet.payload_type < 0x80);
    CHECK_FALSE(packet.is_position());
    CHECK(protocol::is_registration(packet));
    CHECK(protocol::telemetry_type(packet) == 1);
    CHECK(protocol::information_type(packet) == 5);
    // The header byte OGN reads: TelemType in the top two bits, InfoType in the low six.
    CHECK(packet.payload[protocol::kInformationHeaderOffset] == 0x45);
    CHECK(packet.address() == 0x123456u);
    CHECK(packet.address_mapping_table() == 58);

    char name[protocol::kInformationTextBytes + 1] = {0};
    CHECK(protocol::callsign_of(packet, name, sizeof(name)) == 6);
    CHECK(std::string(name) == "D-KXYZ");
}

TEST_CASE("ADS-L.4.SRD860.F.2.1: a registration that is not printable is refused, not drawn") {
    adsl::Packet packet{};
    protocol::from_own_callsign(packet, 0x123456, 58, "D-KXYZ");
    protocol::information_text(packet)[3] = 0x07;

    char name[protocol::kInformationTextBytes + 1] = {0};
    CHECK(protocol::callsign_of(packet, name, sizeof(name)) == 0);
    CHECK(name[0] == 0);

    // And a telemetry record that is not the registration one is not a name either.
    adsl::Packet other{};
    protocol::from_own_callsign(other, 0x123456, 58, "D-KXYZ");
    protocol::set_information_header(other, 0, 5);
    CHECK_FALSE(protocol::is_registration(other));
    CHECK(protocol::callsign_of(other, name, sizeof(name)) == 0);
}

TEST_CASE("ADS-L.4.SRD860.F.2.1: a registration with a comma is refused, not split into PFLAA") {
    adsl::Packet packet{};
    protocol::from_own_callsign(packet, 0x123456, 58, "D-KXYZ");
    protocol::information_text(packet)[1] = ',';

    char name[protocol::kInformationTextBytes + 1] = {0};
    CHECK(protocol::callsign_of(packet, name, sizeof(name)) == 0);
    CHECK(name[0] == 0);
}

TEST_CASE("ADS-L.4.SRD860.F.2.2: the sender address is a 6-bit table and 24 bits of address") {
    adsl::Packet packet{};
    packet.reset();
    packet.set_address_mapping_table(63);
    packet.set_address(0x00FFFFFFu);
    CHECK(packet.address_mapping_table() == 63);
    CHECK(packet.address() == 0x00FFFFFFu);

    // Table 9 and up split the address: an 8-bit manufacturer prefix, then a 16-bit base.
    packet.set_address_mapping_table(9);
    packet.set_address(0xAB1234u);
    CHECK(packet.address_mapping_table() == 9);
    CHECK((packet.address() >> 16) == 0xABu);
    CHECK((packet.address() & 0xFFFFu) == 0x1234u);
    CHECK(packet.table_and_address() == ((9u << 24) | 0xAB1234u));

    CHECK(int(settings::kSkyblipAddressMappingTable) == 58);
    const adsl::Packet ours =
        traffic_packet(settings::address_of(0x1234), settings::kSkyblipAddressMappingTable);
    CHECK(ours.address_mapping_table() == 58);
    CHECK((ours.address() >> 16) == settings::kSkyblipManufacturerPrefix);
    CHECK((ours.address() & 0xFFFFu) == 0x1234u);
}

// Our reading, which EASA is asked to confirm: every payload goes under table 5 while one is set.
TEST_CASE("ADS-L.4.SRD860.F.2.3: an ICAO address goes on the air exactly as it was configured") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    go::Settings& settings = simulator.product().settings();
    std::strncpy(settings.callsign, "D-KXYZ", go::kCallsignBytes - 1);
    REQUIRE(apply_patch(settings, "{\"own_icao\":3934737}") == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(50);
    run_on(simulator, past_settling(simulator), 21000);
    CHECK(fewest_of_each_payload_from(simulator.world().air(), 5, 0x3C0A11u) >= 2);
}

// "This setting shall be revertible by user configuration": 0 clears it, from the next burst on.
TEST_CASE("ADS-L.4.SRD860.F.2.3: a cleared ICAO address puts the manufacturer's back on the air") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    go::Settings& settings = simulator.product().settings();
    std::strncpy(settings.callsign, "D-KXYZ", go::kCallsignBytes - 1);
    REQUIRE(apply_patch(settings, "{\"own_icao\":3934737}") == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(50);
    const uint32_t from_ms = past_settling(simulator);
    run_on(simulator, from_ms, 10000);
    simulator.world().air().clear();
    REQUIRE(apply_patch(settings, "{\"own_icao\":0}") == Status::Ok);
    run_on(simulator, from_ms + 10000 + simulator::Simulator::kStepMs, 21000);
    CHECK(
        fewest_of_each_payload_from(simulator.world().air(), settings::kSkyblipAddressMappingTable,
                                    settings::address_of(platform::host::Identity::kSerial)) >= 2);
}

// "The ADS-L system shall enforce this by disallowing inconsistent user configurations."
TEST_CASE("ADS-L.4.SRD860.F.2.3: an address no aircraft holds, or not the one aboard, is refused") {
    go::Settings settings = go::defaults();
    CHECK(apply_patch(settings, "{\"own_icao\":16777215}") == Status::OutOfRange);
    CHECK(apply_patch(settings, "{\"own_icao\":3934737}", 0x3C6586u) == Status::Conflict);
    CHECK(settings.own_icao == go::kOwnIcaoUnset);
    CHECK(apply_patch(settings, "{\"own_icao\":3958150}", 0x3C6586u) == Status::Ok);
    CHECK(apply_patch(settings, "{\"own_icao\":0}", 0x3C6586u) == Status::Ok);
}

// INFO: fc 13nov26 optional: F.2.4 says a device "may be operated in privacy mode", ours is not
TEST_CASE("ADS-L.4.SRD860.F.2.4: privacy mode selects table 0, the random one" * doctest::skip()) {
    FAIL("optional: no privacy mode, so the table is the one configured and nothing switches it");
}

// A host cannot name the address or the table: own_icao is how an ICAO address gets on air.
TEST_CASE("ADS-L.4.SRD860.F.2.3: a table the configured address does not belong to is refused") {
    go::Settings settings = go::defaults();
    // 0x3C0A11 under ICAO
    const events::RxFrame icao =
        config_frame("{\"command\":\"set\",\"address\":3934737,\"address_mapping_table\":5}");
    CHECK(go::apply_settings(settings, cbor::Reader(ConstByteSpan(icao.data.data(), icao.bytes))) ==
          Status::Ok);

    uint8_t frame[64];
    cbor::Writer writer(frame, sizeof(frame));
    go::write_config_fields(writer, settings, 0x123456);
    const cbor::Reader reader(ConstByteSpan(frame, static_cast<size_t>(writer.finish())));
    CHECK(reader.read_unsigned(link::config_key::kAddress).value() == 0x123456u);
    CHECK(reader.read_unsigned(link::config_key::kAddressMappingTable).value() == 58u);
}
