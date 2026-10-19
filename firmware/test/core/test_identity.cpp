// The activation messages byte for byte against the schema's vectors, then the unit verifying them.
#include <cstring>
#include <string>
#include <vector>

#include "core/crypto/ed25519.h"
#include "core/identity/activation.h"
#include "core/identity/messages.h"
#include "core/identity/unit.h"
#include "core/identity/unlock_code.h"
#include "core/util/hex.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/identity.h"
#include "test/support/activation_vectors.h"

using namespace skyblip;
using crypto::Ed25519;
using platform::host::Identity;

namespace {

bool same(ConstByteSpan bytes, const std::vector<uint8_t>& expected) {
    return bytes.size() == expected.size() &&
           std::memcmp(bytes.data(), expected.data(), expected.size()) == 0;
}

std::string hex_of(ConstByteSpan bytes) {
    std::string out(2 * bytes.size(), '?');
    hex::write(bytes, out.data());
    return out;
}

struct Exchange {
    ActivationVectors v;
    Ed25519::Seed server_seed = v.bytes<32>("server_seed");
    Ed25519::PublicKey server_key = v.bytes<32>("server_public_key");
    Ed25519::Seed unit_seed = v.bytes<32>("unit_seed");
    Ed25519::PublicKey unit_key = v.bytes<32>("unit_public_key");
    identity::DeviceId device_id = v.bytes<8>("device_id");
    identity::Nonce unit_nonce = v.bytes<16>("unit_nonce");
    identity::Nonce server_nonce = v.bytes<16>("server_nonce");
    std::string version = v.text("version");
    bool release = v.flag("release");
    uint16_t serial = static_cast<uint16_t>(v.number("serial"));
};

}  // namespace

TEST_CASE(
    "identity: the vectors' keys are the keys their seeds make, and the server's id is theirs") {
    Exchange x;
    CHECK(Ed25519::public_key_of(x.server_seed) == x.server_key);
    CHECK(Ed25519::public_key_of(x.unit_seed) == x.unit_key);
    const identity::ServerKeyId id = identity::server_key_id(x.server_key);
    CHECK(hex_of(ConstByteSpan(id.data(), id.size())) == x.v.text("server_key_id"));
}

TEST_CASE("identity: the register message is the label, Nu and Ns, and the server's R checks") {
    Exchange x;
    const identity::Message m = identity::register_message(x.unit_nonce, x.server_nonce);
    CHECK(same(m.bytes(), x.v.message("register_message")));
    const auto r = x.v.bytes<64>("register_signature");
    CHECK(Ed25519::sign(x.server_seed, m.bytes()) == r);
    CHECK(Ed25519::verify(r, x.server_key, m.bytes()));
}

// Ed25519 is deterministic, so the unit's S comes out byte-identical to the server's.
TEST_CASE(
    "identity: the registration message ends in the version, and the unit's S is the vectors'") {
    Exchange x;
    const identity::Message m = identity::registration_message(
        x.device_id, x.unit_key, x.unit_nonce, x.server_nonce,
        identity::server_key_id(x.server_key), x.release, x.version.c_str());
    CHECK(same(m.bytes(), x.v.message("registration_message")));
    const auto s = x.v.bytes<64>("registration_signature");
    CHECK(Ed25519::sign(x.unit_seed, m.bytes()) == s);
    CHECK(Ed25519::verify(s, x.unit_key, m.bytes()));
}

TEST_CASE("identity: the unlock message carries the serial big-endian, and the server's U checks") {
    Exchange x;
    REQUIRE(x.serial == 298);
    const identity::Message m =
        identity::unlock_message(x.device_id, x.unit_key, x.serial, x.unit_nonce);
    CHECK(same(m.bytes(), x.v.message("unlock_message")));
    const auto u = x.v.bytes<64>("unlock_signature");
    CHECK(Ed25519::sign(x.server_seed, m.bytes()) == u);
    CHECK(Ed25519::verify(u, x.server_key, m.bytes()));
}

TEST_CASE("identity: no signature passes for another purpose, the labels see to that") {
    Exchange x;
    const auto r = x.v.bytes<64>("register_signature");
    const identity::Message unlock =
        identity::unlock_message(x.device_id, x.unit_key, x.serial, x.unit_nonce);
    CHECK_FALSE(Ed25519::verify(r, x.server_key, unlock.bytes()));
    const auto u = x.v.bytes<64>("unlock_signature");
    CHECK_FALSE(Ed25519::verify(u, x.server_key,
                                identity::register_message(x.unit_nonce, x.server_nonce).bytes()));
}

// The id of RFC 8032's TEST 1 key, from Python's hashlib: sha256(key)[:4] is 21fe31df.
TEST_CASE("identity: a server key is named by the first four bytes of its SHA-256") {
    Ed25519::PublicKey key{};
    REQUIRE(hex::read("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
                      ByteSpan(key.data(), key.size())));
    const identity::ServerKeyId id = identity::server_key_id(key);
    CHECK(hex_of(ConstByteSpan(id.data(), id.size())) == "21fe31df");
}

TEST_CASE("identity: hex is lowercase both ways, and a read takes exactly the digits it is owed") {
    uint8_t out[2] = {0, 0};
    CHECK(hex::read("0aff", ByteSpan(out, 2)));
    CHECK(out[0] == 0x0A);
    CHECK(out[1] == 0xFF);
    CHECK_FALSE(hex::read("0AFF", ByteSpan(out, 2)));
    CHECK_FALSE(hex::read("0af", ByteSpan(out, 2)));
    CHECK_FALSE(hex::read("0aff00", ByteSpan(out, 2)));
    CHECK_FALSE(hex::read("0ag0", ByteSpan(out, 2)));
    CHECK(hex_of(ConstByteSpan(out, 2)) == "0aff");
}

TEST_CASE("identity: the unlock page holds serial, Nu and U, and an erased page holds nothing") {
    identity::UnlockCode code{};
    code.serial = 0x012A;
    code.nonce.fill(0x11);
    code.signature.fill(0x22);
    const identity::UnlockPage page = identity::page_of(code);
    CHECK(page.size() == 84);
    CHECK(page[0] == 0x01);
    CHECK(page[1] == 0x2A);
    CHECK(page[2] == 0x11);
    CHECK(page[18] == 0x22);
    CHECK(page[82] == 0xFF);

    const std::optional<identity::UnlockCode> back = identity::code_on(page);
    REQUIRE(back);
    CHECK(back->serial == code.serial);
    CHECK(back->nonce == code.nonce);
    CHECK(back->signature == code.signature);

    identity::UnlockPage erased{};
    erased.fill(0xFF);
    CHECK_FALSE(identity::code_on(erased));
}

TEST_CASE("identity: the name says activate and the tag until a serial, then product and address") {
    char name[identity::kBluetoothNameCap + 1];
    identity::Activation blank{};
    blank.bluetooth_tag = 0x3F2A;
    identity::bluetooth_name(name, sizeof(name), "skyBlip Go+", blank);
    CHECK(std::string(name) == "activate 3F2A skyBlip");

    identity::Activation activated = blank;
    activated.serial = 0x012A;
    identity::bluetooth_name(name, sizeof(name), "skyBlip Go+", activated);
    CHECK(std::string(name) == "skyBlip Go+ 00012A");
    identity::bluetooth_name(name, sizeof(name), "skyBlip Go", activated);
    CHECK(std::string(name) == "skyBlip Go 00012A");
}

// The host trusts the vectors' server, so the bench and the server's suite check one exchange.
TEST_CASE("identity: the host platform is the vectors' unit and trusts the vectors' server") {
    Exchange x;
    Identity port(std::nullopt);
    CHECK(port.trusted_server_key() == x.server_key);
    CHECK(port.device_id() == x.device_id);
    Ed25519::Seed seed{};
    REQUIRE(port.seed(seed));
    CHECK(seed == x.unit_seed);
}

TEST_CASE("identity: a unit is activated at load only by a code its trusted key signed for it") {
    SUBCASE("the code the vectors' server signed") {
        Exchange x;
        Identity port(std::nullopt);
        port.stored =
            identity::UnlockCode{x.serial, x.unit_nonce, x.v.bytes<64>("unlock_signature")};
        identity::Unit unit(port);
        unit.load();
        CHECK(unit.keyed());
        CHECK(unit.activation().serial == x.serial);
        CHECK(unit.activation().address() == settings::AirAddress{0x00012Au});
    }
    SUBCASE("the same code with another serial in front of it") {
        Exchange x;
        Identity port(std::nullopt);
        port.stored = identity::UnlockCode{uint16_t(x.serial + 1), x.unit_nonce,
                                           x.v.bytes<64>("unlock_signature")};
        identity::Unit unit(port);
        unit.load();
        CHECK_FALSE(unit.activation().activated());
    }
    SUBCASE("a code another key signed: what a development unit carries into production") {
        Identity port(std::nullopt);
        const Ed25519::Seed other_server{1, 2, 3};
        identity::UnlockCode code{0x0042, {}, {}};
        code.signature = Ed25519::sign(
            other_server, identity::unlock_message(Identity::kDeviceId,
                                                   Ed25519::public_key_of(Identity::kUnitSeed),
                                                   code.serial, code.nonce)
                              .bytes());
        port.stored = code;
        identity::Unit unit(port);
        unit.load();
        CHECK_FALSE(unit.activation().activated());
    }
    SUBCASE("a code the trusted key signed over another DEVICEID") {
        Identity port(std::nullopt);
        identity::DeviceId other = Identity::kDeviceId;
        other[7] ^= 0x01;
        identity::UnlockCode code{0x0042, {}, {}};
        code.signature = Ed25519::sign(
            Identity::kTestServerSeed,
            identity::unlock_message(other, Ed25519::public_key_of(Identity::kUnitSeed),
                                     code.serial, code.nonce)
                .bytes());
        port.stored = code;
        identity::Unit unit(port);
        unit.load();
        CHECK_FALSE(unit.activation().activated());
    }
    SUBCASE("a code the trusted key signed over another public key") {
        Identity port(std::nullopt);
        const Ed25519::Seed other_unit{7, 7, 7};
        identity::UnlockCode code{0x0042, {}, {}};
        code.signature = Ed25519::sign(
            Identity::kTestServerSeed,
            identity::unlock_message(Identity::kDeviceId, Ed25519::public_key_of(other_unit),
                                     code.serial, code.nonce)
                .bytes());
        port.stored = code;
        identity::Unit unit(port);
        unit.load();
        CHECK_FALSE(unit.activation().activated());
    }
    SUBCASE("a code the trusted key signed for serial 0") {
        Identity port(std::nullopt);
        port.stored = Identity::unlock_code_for(0, identity::Nonce{});
        identity::Unit unit(port);
        unit.load();
        CHECK_FALSE(unit.activation().activated());
    }
    SUBCASE("a unit whose key could not be read") {
        Identity port(Identity::kSerial);
        port.keyed = false;
        identity::Unit unit(port);
        unit.load();
        CHECK_FALSE(unit.keyed());
        CHECK_FALSE(unit.activation().activated());
    }
    SUBCASE("an erased page") {
        Identity port(std::nullopt);
        identity::Unit unit(port);
        unit.load();
        CHECK(unit.keyed());
        CHECK_FALSE(unit.activation().activated());
        CHECK(unit.activation().bluetooth_tag == Identity::kBluetoothTag);
    }
}
