// ADS-L 4 SRD-860 issue 2 Subpart F: the ADS-L header, who a packet is from and what it carries.
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>

#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/settings/address.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/settings.h"

using namespace skyblip;

namespace {
protocol::AdslPacket traffic_packet(uint32_t addr = 0x123456, uint8_t table = 6) {
    model::OwnState own{};
    own.fix_valid = true;
    own.utc_valid = true;
    own.lat_1e7 = 481234500;
    own.lon_1e7 = 81234500;
    own.alt_mm = 1234000;
    own.speed_mm_s = 45000;  // 180 quarter-m/s on the wire
    own.hdop_e2 = 90;
    own.flight_state = 2;
    protocol::AdslPacket p{};
    protocol::from_own(p, own, addr, table, 4);
    return p;
}
}  // namespace

// 40 bits of header and 120 of Traffic payload: 20 bytes, which is the block E.2 scrambles whole.
TEST_CASE("ADS-L.4.SRD860.F.1: the ADS-L data is a 40-bit header and a payload behind it") {
    const int header_bits = 40;
    const int traffic_payload_bits = 120;
    CHECK((header_bits + traffic_payload_bits) / 8 == 20);
    CHECK(sizeof(protocol::AdslPacket{}.Byte) == 20);
    // The clause's rule for a payload width: 24 + N*32 bits.
    CHECK((traffic_payload_bits - 24) % 32 == 0);
}

TEST_CASE("ADS-L.4.SRD860.F.2: the header is a type byte, 30 bits of sender, reserved, relay") {
    protocol::AdslPacket p = traffic_packet();
    CHECK(p.Type == 0x02);
    CHECK(p.address() == 0x123456u);
    CHECK(p.addr_table() == 6);
    CHECK_FALSE(p.is_relay());

    p.set_relay();
    CHECK(p.is_relay());
    CHECK((p.Address[3] & 0x80) != 0);

    // One little-endian word: the AMT in bits 0 to 5, the address in bits 6 to 29.
    CHECK((protocol::AdslPacket::get4(p.Address) & 0x3Fu) == 6u);
    CHECK(((protocol::AdslPacket::get4(p.Address) >> 6) & 0x00FFFFFFu) == 0x123456u);
}

TEST_CASE("ADS-L.4.SRD860.F.2.1: a Traffic payload is type 2, in the broadcast half of the range") {
    protocol::AdslPacket p = traffic_packet();
    CHECK(p.Type == 0x02);
    CHECK(p.is_position());
    CHECK(p.Type < 0x80);
}

// Issue 2 puts Status, Remote ID and the uplinks on this band, and none of them is an aeroplane.
TEST_CASE("ADS-L.4.SRD860.F.2.1: a payload that is not Traffic never becomes an aircraft") {
    model::AircraftObs obs{};
    for (uint8_t type : {uint8_t(0x00), uint8_t(0x03), uint8_t(0x04), uint8_t(0x05), uint8_t(0x06),
                         uint8_t(0x42), uint8_t(0x82)}) {
        protocol::AdslPacket p = traffic_packet();
        p.Type = type;
        CHECK_FALSE(p.is_position());
        CHECK_FALSE(protocol::to_obs(p, events::Stamp{}, -80, model::Source::AdslDirect, obs));
        CHECK_FALSE(obs.position_valid);
    }
}

// Payload 66 is the entry F.2.1 assigns to OGN and leaves outside its own scope.
TEST_CASE("ADS-L.4.SRD860.F.2.1: payload 66 carries the registration ADS-L has no field for") {
    protocol::AdslPacket p{};
    protocol::from_own_callsign(p, 0x123456, 58, "D-KXYZ");

    CHECK(p.Type == 0x42);
    CHECK(p.Type == 66);
    CHECK(p.Type < 0x80);
    CHECK_FALSE(p.is_position());
    CHECK(p.is_registration());
    CHECK(p.telem_type() == 1);
    CHECK(p.info_type() == 5);
    // The header byte OGN reads: TelemType in the top two bits, InfoType in the low six.
    CHECK(p.Byte[protocol::AdslPacket::kInfoHeaderByte] == 0x45);
    CHECK(p.address() == 0x123456u);
    CHECK(p.addr_table() == 58);

    char name[protocol::AdslPacket::kInfoMsgBytes + 1] = {0};
    CHECK(protocol::callsign_of(p, name, sizeof(name)) == 6);
    CHECK(std::string(name) == "D-KXYZ");
}

TEST_CASE("ADS-L.4.SRD860.F.2.1: a registration that is not printable is refused, not drawn") {
    protocol::AdslPacket p{};
    protocol::from_own_callsign(p, 0x123456, 58, "D-KXYZ");
    p.info_msg()[3] = 0x07;

    char name[protocol::AdslPacket::kInfoMsgBytes + 1] = {0};
    CHECK(protocol::callsign_of(p, name, sizeof(name)) == 0);
    CHECK(name[0] == 0);

    // And a telemetry record that is not the registration one is not a name either.
    protocol::AdslPacket other{};
    protocol::from_own_callsign(other, 0x123456, 58, "D-KXYZ");
    other.set_info_header(0, 5);
    CHECK_FALSE(other.is_registration());
    CHECK(protocol::callsign_of(other, name, sizeof(name)) == 0);
}

TEST_CASE("ADS-L.4.SRD860.F.2.1: a registration with a comma is refused, not split into PFLAA") {
    protocol::AdslPacket p{};
    protocol::from_own_callsign(p, 0x123456, 58, "D-KXYZ");
    p.info_msg()[1] = ',';

    char name[protocol::AdslPacket::kInfoMsgBytes + 1] = {0};
    CHECK(protocol::callsign_of(p, name, sizeof(name)) == 0);
    CHECK(name[0] == 0);
}

TEST_CASE("ADS-L.4.SRD860.F.2.2: the sender address is a 6-bit table and 24 bits of address") {
    protocol::AdslPacket p{};
    p.init();
    p.set_addr_table(63);
    p.set_address(0x00FFFFFFu);
    CHECK(p.addr_table() == 63);
    CHECK(p.address() == 0x00FFFFFFu);

    // Table 9 and up split the address: an 8-bit manufacturer prefix, then a 16-bit base.
    p.set_addr_table(9);
    p.set_address(0xAB1234u);
    CHECK(p.addr_table() == 9);
    CHECK((p.address() >> 16) == 0xABu);
    CHECK((p.address() & 0xFFFFu) == 0x1234u);
    CHECK(p.address_and_type() == ((9u << 24) | 0xAB1234u));

    CHECK(int(settings::kAddrTableSkyblip) == 58);
    CHECK(traffic_packet(0x123456u, settings::kAddrTableSkyblip).addr_table() == 58);
    CHECK(traffic_packet(0x123456u, settings::kAddrTableSkyblip).address() == 0x123456u);
}

// TODO: fc 19sep26 no ICAO entry: the identity is the chip's, and no patch moves it
TEST_CASE("ADS-L.4.SRD860.F.2.3: an ICAO address goes on the air exactly as it was configured" *
          doctest::skip()) {
    CHECK(traffic_packet(0x3C0A11u, 5).address() == 0x3C0A11u);
    FAIL("the packet carries any table it is handed, and no configuration path hands it table 5");
}

// TODO: fc 19sep26 no privacy mode on this device: the stealth setting and its table 0 are gone
TEST_CASE("ADS-L.4.SRD860.F.2.4: privacy mode selects table 0, the random one" * doctest::skip()) {
    FAIL("the address table is the one a pilot configured, and nothing switches it at transmit");
}

// The clause asks a device to disallow an inconsistent configuration: this one cannot make any.
TEST_CASE("ADS-L.4.SRD860.F.2.3: a table the configured address does not belong to is refused") {
    go::Settings s = go::defaults();
    const char* icao = "{\"addr\":3934737,\"addr_table\":5}";  // 0x3C0A11 under ICAO
    CHECK(go::apply_json(s, icao, static_cast<int>(strlen(icao))) == Status::Ok);

    char buf[256];
    const int n = go::to_json(s, 0x123456, buf, static_cast<int>(sizeof(buf)));
    json::Reader r(buf, n);
    long v = 0;
    CHECK(r.get_int("addr", v));
    CHECK(v == 0x123456);
    CHECK(r.get_int("addr_table", v));
    CHECK(v == 58);
}

// Every ADS-L data block has to be scramblable, and XXTEA works on whole 32-bit words.
TEST_CASE("ADS-L.4.SRD860.F.2.5: header plus payload is a whole number of 32-bit words") {
    const int header_bytes = 5;
    const int traffic_payload_bytes = 15;
    CHECK((header_bytes + traffic_payload_bytes) % 4 == 0);
    // The clause's own list of valid payload sizes: 3, 7, 11, 15, and on by fours.
    CHECK((traffic_payload_bytes - 3) % 4 == 0);
}
