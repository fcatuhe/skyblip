// A Go and a Bravo over a simulated link, faulted on either side as the air would fault it.
#include <cstdint>
#include <string>
#include <vector>

#include "core/util/result.h"
#include "doctest/doctest.h"
#include "test/support/bravo_stub.h"
#include "test/support/config_frame.h"
#include "test/support/log_transfer.h"
#include "test/support/paired_link.h"
#include "test/support/registry_frames.h"

using namespace skyblip;

namespace {

using Direction = PairedLink::Direction;
using Flow = PairedLink::Flow;
using Side = PairedLink::Side;

constexpr uint32_t kPassMs = 50;

const std::vector<BravoStub::Recorded> kRecording = {
    {500,
     "\"address\":4479337,\"at_s\":1785628800,\"into_ms\":250,\"latitude_1e7\":485100000,"
     "\"longitude_1e7\":85200000,\"altitude_m\":1250,\"altitude_age_ms\":0"},
    {1500,
     "\"address\":4479337,\"at_s\":1785628801,\"into_ms\":250,\"latitude_1e7\":485103000,"
     "\"longitude_1e7\":85200000,\"altitude_m\":1252,\"altitude_age_ms\":0,\"speed_q\":240,"
     "\"track_c9\":0,\"speed_age_ms\":0"},
    {2500,
     "\"address\":4479337,\"at_s\":1785628802,\"into_ms\":250,\"callsign\":\"DLH4AB\","
     "\"callsign_heard_s\":1785628802"},
    {3500,
     "\"address\":3949381,\"at_s\":1785628803,\"pressure_altitude_ft\":4500,"
     "\"pressure_altitude_age_ms\":120"},
    {4500,
     "\"address\":3949381,\"at_s\":1785628804,\"pressure_altitude_ft\":4525,"
     "\"pressure_altitude_age_ms\":80"},
};

std::string peer_claim(uint32_t epoch) {
    return complete_registry_frame(
        "{\"command\":\"peer_claim\",\"boot_epoch\":" + std::to_string(epoch) +
        ",\"alarm_requested\":false,\"paired\":false}");
}

std::string peer_claim_with_position() {
    return complete_registry_frame(
        "{\"command\":\"peer_claim\",\"boot_epoch\":7,\"at_s\":1785628800,\"into_ms\":250,"
        "\"latitude_1e7\":485000000,\"longitude_1e7\":85000000,\"position_age_ms\":200,"
        "\"altitude_hae_mm\":1250000,\"altitude_hae_age_ms\":200,\"alarm_requested\":false,"
        "\"paired\":false}");
}

struct Pair {
    BravoStub bravo{kRecording};
    PairedLink link{bravo.link()};
    uint32_t now_ms{0};

    void run_until(uint32_t until_ms) {
        for (; now_ms < until_ms; now_ms += kPassMs) {
            bravo.run_pass(now_ms);
            link.pump();
        }
    }

    std::vector<std::string> received() {
        std::vector<std::string> frames;
        std::string frame;
        while (link.take(frame)) frames.push_back(link_text(frame));
        return frames;
    }
};

std::vector<std::string> commands(const std::vector<std::string>& frames) {
    std::vector<std::string> out;
    for (const std::string& frame : frames) out.push_back(field(frame, "command"));
    return out;
}

}  // namespace

TEST_CASE("paired link: Bravo's recorded observations and its health reach the central in order") {
    Pair pair;
    REQUIRE(pair.link.connect());
    pair.run_until(2000);

    const std::vector<std::string> frames = pair.received();
    REQUIRE(commands(frames) ==
            std::vector<std::string>{"health", "observation", "health", "observation"});
    const std::string epoch = std::to_string(pair.bravo.boot_epoch());
    CHECK(frames[0] == "{\"command\":\"health\",\"protocol_version\":1,\"boot_epoch\":" + epoch +
                           ",\"listening\":true,\"utc_valid\":true,\"own_icao_set\":false,"
                           "\"alarm_granted\":false,\"export_reference\":\"own\",\"paired\":true,"
                           "\"charging\":false,\"power_level\":\"normal\"}");
    CHECK(frames[1] == "{\"command\":\"observation\",\"address\":4479337,\"boot_epoch\":" + epoch +
                           ",\"sequence\":0,\"at_s\":1785628800,\"into_ms\":250,"
                           "\"latitude_1e7\":485100000,\"longitude_1e7\":85200000,"
                           "\"altitude_m\":1250,\"altitude_age_ms\":0}");
    CHECK(field(frames[2], "last_sequence") == "0");
    CHECK(field(frames[3], "sequence") == "1");
    CHECK(pair.link.delivered(Direction::ToCentral) == 4);
}

TEST_CASE("paired link: the central's peer_claim reaches Bravo on the peer endpoint") {
    Pair pair;
    REQUIRE(pair.link.connect());
    CHECK(pair.link.write(peer_claim(12)) == Status::Ok);
    pair.run_until(100);

    REQUIRE(pair.bravo.claims.size() == 1);
    CHECK(pair.bravo.claims[0] ==
          "{\"command\":\"peer_claim\",\"boot_epoch\":12,\"alarm_requested\":false,"
          "\"paired\":false}");
    CHECK(pair.link.delivered(Direction::ToPeripheral) == 1);
}

TEST_CASE("paired link: a lost direction drops its frames while the other keeps flowing") {
    Pair pair;
    REQUIRE(pair.link.connect());
    pair.link.set_flow(Direction::ToCentral, Flow::Lost);
    CHECK(pair.link.write(peer_claim(1)) == Status::Ok);
    pair.run_until(2000);

    CHECK(pair.received().empty());
    CHECK(pair.link.lost(Direction::ToCentral) == 4);
    CHECK(pair.bravo.claims.size() == 1);

    pair.link.set_flow(Direction::ToCentral, Flow::Flowing);
    pair.link.set_flow(Direction::ToPeripheral, Flow::Lost);
    CHECK(pair.link.write(peer_claim(2)) == Status::Ok);
    pair.run_until(3000);

    CHECK(commands(pair.received()) == std::vector<std::string>{"health", "observation"});
    CHECK(pair.bravo.claims.size() == 1);
    CHECK(pair.link.lost(Direction::ToPeripheral) == 1);
    CHECK(pair.link.up(Side::Central));
    CHECK(pair.link.up(Side::Peripheral));
}

TEST_CASE(
    "paired link: a stall holds both directions in order on a live link, then releases them") {
    Pair pair;
    REQUIRE(pair.link.connect());
    pair.link.set_flow(Flow::Stalled);
    CHECK(pair.link.write(peer_claim(1)) == Status::Ok);
    CHECK(pair.link.write(peer_claim(2)) == Status::Ok);
    pair.run_until(2000);

    CHECK(pair.received().empty());
    CHECK(pair.bravo.claims.empty());
    CHECK(pair.bravo.link().up());
    CHECK(pair.link.lost(Direction::ToCentral) == 0);

    pair.link.set_flow(Flow::Flowing);
    pair.link.pump();
    const std::vector<std::string> frames = pair.received();
    REQUIRE(commands(frames) ==
            std::vector<std::string>{"health", "observation", "health", "observation"});
    CHECK(field(frames[3], "sequence") == "1");

    pair.run_until(2050);
    REQUIRE(pair.bravo.claims.size() == 2);
    CHECK(field(pair.bravo.claims[0], "boot_epoch") == "1");
    CHECK(field(pair.bravo.claims[1], "boot_epoch") == "2");
}

TEST_CASE("paired link: a drop loses what was in flight, and a reconnect is a new connection") {
    Pair pair;
    REQUIRE(pair.link.connect());
    const uint32_t first = pair.bravo.link().connection(1);
    pair.link.set_flow(Flow::Stalled);
    CHECK(pair.link.write(peer_claim(1)) == Status::Ok);
    pair.run_until(1000);

    pair.link.drop();
    CHECK(pair.link.lost(Direction::ToCentral) == 2);
    CHECK(pair.link.lost(Direction::ToPeripheral) == 1);
    CHECK_FALSE(pair.bravo.link().up());
    CHECK(pair.link.write(peer_claim(2)) == Status::Down);
    pair.run_until(2000);

    pair.link.set_flow(Flow::Flowing);
    REQUIRE(pair.link.connect());
    CHECK(pair.bravo.link().connection(1) != first);
    pair.run_until(3000);

    // Bravo counted the observation the drop lost, so the first one the central reads is 1.
    const std::vector<std::string> frames = pair.received();
    REQUIRE(commands(frames) == std::vector<std::string>{"health", "observation"});
    CHECK(field(frames[1], "sequence") == "1");
    CHECK(field(frames[1], "at_s") == "1785628802");
}

TEST_CASE("paired link: a rebooted Bravo comes back under a new epoch once the central times out") {
    Pair pair;
    REQUIRE(pair.link.connect());
    pair.run_until(2000);
    const uint32_t first_epoch = pair.bravo.boot_epoch();
    CHECK(pair.received().size() == 4);

    pair.link.reboot_peripheral(pair.bravo.reboot());
    CHECK(pair.link.up(Side::Central));
    CHECK_FALSE(pair.link.up(Side::Peripheral));
    CHECK(pair.link.write(peer_claim(1)) == Status::Ok);
    pair.run_until(3000);
    CHECK(pair.received().empty());
    CHECK(pair.bravo.claims.empty());
    CHECK(pair.link.lost(Direction::ToPeripheral) == 1);
    CHECK_FALSE(pair.link.connect());

    pair.link.drop();
    REQUIRE(pair.link.connect());
    CHECK(pair.bravo.link().connection(1) == kFirstConnection);
    pair.run_until(4000);

    const std::vector<std::string> frames = pair.received();
    REQUIRE(commands(frames) == std::vector<std::string>{"health", "observation"});
    CHECK(pair.bravo.boot_epoch() != first_epoch);
    CHECK(field(frames[0], "boot_epoch") == std::to_string(pair.bravo.boot_epoch()));
    CHECK(field(frames[0], "last_sequence").empty());
    CHECK(field(frames[1], "sequence") == "0");
    CHECK(field(frames[1], "address") == "3949381");
}

TEST_CASE(
    "paired link: a rebooted central loses what it had not read, and Bravo holds on until it times "
    "out") {
    Pair pair;
    REQUIRE(pair.link.connect());
    pair.run_until(1000);

    pair.link.reboot_central();
    CHECK(pair.received().empty());
    CHECK(pair.link.write(peer_claim(1)) == Status::Down);
    CHECK_FALSE(pair.link.connect());
    pair.run_until(2000);
    CHECK(pair.bravo.link().up());
    CHECK(pair.link.lost(Direction::ToCentral) == 2);

    pair.link.drop();
    CHECK_FALSE(pair.bravo.link().up());
    REQUIRE(pair.link.connect());
    CHECK(pair.link.write(peer_claim(2)) == Status::Ok);
    pair.run_until(3000);

    CHECK(commands(pair.received()) == std::vector<std::string>{"health", "observation"});
    REQUIRE(pair.bravo.claims.size() == 1);
    CHECK(field(pair.bravo.claims[0], "boot_epoch") == "2");
}

TEST_CASE("paired link: a peer frame wider than the link's payload is refused on either side") {
    Pair pair;
    pair.bravo.link().declare_payload_bytes(40);
    REQUIRE(pair.link.connect());
    pair.run_until(1000);

    CHECK(commands(pair.received()) == std::vector<std::string>{"health"});
    CHECK(pair.bravo.refused == 1);
    CHECK(pair.bravo.link().refused_oversize == 1);
    CHECK(pair.link.write(peer_claim_with_position()) == Status::OutOfRange);
    CHECK(pair.link.write(peer_claim(1)) == Status::Ok);
}

TEST_CASE(
    "paired link: a central write past what Bravo's link carries is refused at the boundary") {
    Pair pair;
    pair.bravo.link().declare_payload_bytes(495);
    REQUIRE(pair.link.connect());

    CHECK(pair.link.write(std::string(sizeof(events::RxFrame::data) + 1, '\0')) ==
          Status::OutOfRange);
    CHECK(pair.link.write(std::string(sizeof(events::RxFrame::data), '\0')) == Status::Ok);
    pair.link.pump();
    CHECK(pair.link.delivered(Direction::ToPeripheral) == 1);
}

TEST_CASE("paired link: a central write past the payload is refused at the boundary") {
    Pair pair;
    pair.bravo.link().declare_payload_bytes(40);
    REQUIRE(pair.link.connect());

    CHECK(pair.link.write(std::string(41, '\0')) == Status::OutOfRange);
    CHECK(pair.link.write(std::string(40, '\0')) == Status::Ok);
}

TEST_CASE("paired link: writes Bravo cannot take yet wait for its next pass, none lost") {
    Pair pair;
    REQUIRE(pair.link.connect());
    pair.link.set_flow(Flow::Stalled);
    for (uint32_t epoch = 1; epoch <= 6; epoch++)
        CHECK(pair.link.write(peer_claim(epoch)) == Status::Ok);
    pair.run_until(1000);

    pair.link.set_flow(Flow::Flowing);
    pair.link.pump();
    CHECK(pair.link.delivered(Direction::ToPeripheral) == 4);
    pair.run_until(1150);

    CHECK(pair.link.delivered(Direction::ToPeripheral) == 6);
    CHECK(pair.link.lost(Direction::ToPeripheral) == 0);
    REQUIRE(pair.bravo.claims.size() == 6);
    for (size_t i = 0; i < 6; i++)
        CHECK(field(pair.bravo.claims[i], "boot_epoch") == std::to_string(i + 1));
}

TEST_CASE("paired link: two centrals on one Bravo each receive what it sends to all") {
    BravoStub bravo{kRecording};
    PairedLink go{bravo.link(), 1};
    PairedLink stranger{bravo.link(), 2};
    REQUIRE(go.connect());
    REQUIRE(stranger.connect());
    for (uint32_t now_ms = 0; now_ms < 1000; now_ms += kPassMs) {
        bravo.run_pass(now_ms);
        go.pump();
        stranger.pump();
    }

    CHECK(go.delivered(Direction::ToCentral) == 2);
    CHECK(stranger.delivered(Direction::ToCentral) == 2);
    std::string first;
    std::string second;
    REQUIRE(go.take(first));
    REQUIRE(stranger.take(second));
    CHECK(first == second);
}

TEST_CASE(
    "paired link: Bravo sends no observation without UTC or a reference, and skips its sequence") {
    Pair pair;
    REQUIRE(pair.link.connect());
    pair.bravo.health.utc_valid = false;
    pair.run_until(1000);
    pair.bravo.health.utc_valid = true;
    pair.bravo.health.export_reference = "none";
    pair.run_until(2000);
    pair.bravo.health.export_reference = "go";
    pair.run_until(3050);

    const std::vector<std::string> frames = pair.received();
    REQUIRE(commands(frames) ==
            std::vector<std::string>{"health", "health", "health", "observation", "health"});
    CHECK(field(frames[0], "utc_valid") == "false");
    CHECK(field(frames[1], "export_reference") == "none");
    CHECK(field(frames[2], "last_sequence").empty());
    CHECK(field(frames[3], "sequence") == "0");
    CHECK(field(frames[3], "at_s") == "1785628802");
    CHECK(field(frames[4], "last_sequence") == "0");
}

TEST_CASE(
    "paired link: Bravo's health after a long silent stretch resumes on its next second, not in a "
    "burst") {
    Pair pair;
    REQUIRE(pair.link.connect());
    pair.bravo.run_pass(0);
    pair.bravo.run_pass(5300);
    pair.bravo.run_pass(5350);
    pair.bravo.run_pass(5950);
    pair.bravo.run_pass(6000);
    pair.link.pump();

    std::vector<std::string> health;
    for (const std::string& frame : pair.received())
        if (field(frame, "command") == "health") health.push_back(frame);
    CHECK(health.size() == 3);
}

TEST_CASE("paired link: a stall in one direction leaves the other flowing") {
    Pair pair;
    REQUIRE(pair.link.connect());
    pair.link.set_flow(Direction::ToPeripheral, Flow::Stalled);
    CHECK(pair.link.write(peer_claim(1)) == Status::Ok);
    pair.run_until(1000);

    CHECK(commands(pair.received()) == std::vector<std::string>{"health", "observation"});
    CHECK(pair.bravo.claims.empty());

    pair.link.set_flow(Direction::ToPeripheral, Flow::Flowing);
    pair.run_until(1100);
    CHECK(pair.bravo.claims.size() == 1);
    CHECK(pair.link.lost(Direction::ToPeripheral) == 0);
}

TEST_CASE("paired link: what a rebooting Bravo sent but the link had not carried is counted lost") {
    Pair pair;
    REQUIRE(pair.link.connect());
    pair.run_until(1000);
    pair.bravo.run_pass(1000);

    platform::host::Link& rebooted = pair.bravo.reboot();
    pair.link.reboot_peripheral(rebooted);
    CHECK(pair.link.lost(Direction::ToCentral) == 1);
    CHECK(commands(pair.received()) == std::vector<std::string>{"health", "observation"});
}

TEST_CASE(
    "paired link: a case clearing Bravo's sent frames between pumps costs no central a frame") {
    BravoStub bravo{kRecording};
    PairedLink go{bravo.link(), 1};
    PairedLink stranger{bravo.link(), 2};
    REQUIRE(go.connect());
    REQUIRE(stranger.connect());
    bravo.run_pass(0);
    go.pump();
    stranger.pump();
    std::string frame;
    REQUIRE(go.take(frame));
    REQUIRE(stranger.take(frame));

    bravo.run_pass(500);
    bravo.link().clear();
    bravo.run_pass(1000);
    go.pump();
    stranger.pump();

    for (PairedLink* central : {&go, &stranger}) {
        std::vector<std::string> frames;
        while (central->take(frame)) frames.push_back(link_text(frame));
        CHECK(commands(frames) == std::vector<std::string>{"observation", "health"});
        CHECK(central->delivered(Direction::ToCentral) == 3);
        CHECK(central->lost(Direction::ToCentral) == 0);
    }
}
