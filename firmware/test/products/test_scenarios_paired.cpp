// A Go and a Bravo paired in one aircraft: the scenarios fly both over one sky and one link.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "core/comms/peer.h"
#include "core/model/aircraft.h"
#include "core/traffic/table.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/nearby.h"
#include "simulator/paired_simulator.h"
#include "test/support/glass_text.h"

using namespace skyblip;

namespace {

constexpr uint32_t kGa = 0x300002;
constexpr uint32_t kAirliner = 0x300001;
constexpr uint32_t kDual = 0x300003;

struct Paired {
    simulator::PairedSimulator simulator;
    uint32_t now_ms{0};

    explicit Paired(const char* file) {
        REQUIRE(simulator.setup() == Status::Ok);
        REQUIRE(simulator.load_file(file));
    }

    void run_until(uint32_t until_ms) {
        for (; now_ms <= until_ms; now_ms += simulator::PairedSimulator::kStepMs)
            simulator.step(now_ms);
    }

    const bus::State& go() { return simulator.product().state(); }
    comms::BravoState bravo() { return go().bravo; }

    bool reads(const char* text, int scale = 1) {
        return reads_in(simulator.product().screen().framebuffer(), text, 0, 0, go::kGlassWidth,
                        go::kGlassHeight, scale);
    }
    bool reads_lost() { return reads("BRAVO LOST", 2); }

    // INFO: fc 14nov26 the sentences Bravo's phone read since it had read `read` of them
    std::vector<std::string> bravo_sentences_since(uint32_t read, const char* kind) {
        const auto& sentences = simulator.phone().sentences();
        const uint32_t fresh = simulator.phone().sentences_read() - read;
        REQUIRE(fresh <= sentences.size());
        std::vector<std::string> found;
        for (size_t i = sentences.size() - fresh; i < sentences.size(); i++)
            if (sentences[i].rfind(kind, 0) == 0) found.push_back(sentences[i]);
        return found;
    }
};

int slots_of(const bus::State& state, uint32_t address) {
    int slots = 0;
    for (int i = 0; i < traffic::TrafficTable::kMaxTargets; i++) {
        const traffic::Target* target = state.traffic.at(i);
        if (target != nullptr && target->used && target->observation.address == address) slots++;
    }
    return slots;
}

const traffic::Target* target_of(const bus::State& state, uint32_t address) {
    for (int i = 0; i < traffic::TrafficTable::kMaxTargets; i++) {
        const traffic::Target* target = state.traffic.at(i);
        if (target != nullptr && target->used && target->observation.address == address)
            return target;
    }
    return nullptr;
}

bool parses(const char* json) {
    simulator::Scenario scenario;
    return simulator::parse_scenario(json, static_cast<int>(std::strlen(json)), scenario);
}

}  // namespace

TEST_CASE("paired scenario: the parser reads the pair, Bravo's own events and the link") {
    const char* json =
        "{\"product\":\"paired\",\"events\":["
        "{\"at_ms\":0,\"unit\":\"bravo\",\"companion\":1},"
        "{\"at_ms\":5,\"unit\":\"bravo\",\"fix\":0},"
        "{\"at_ms\":6,\"fix\":0},"
        "{\"at_ms\":7,\"bravo_link\":0}]}";
    simulator::Scenario scenario;
    REQUIRE(simulator::parse_scenario(json, static_cast<int>(std::strlen(json)), scenario));
    CHECK(scenario.product == simulator::Product::Paired);
    REQUIRE(scenario.events.size() == 4);
    CHECK(scenario.events[0].on_bravo);
    CHECK(scenario.events[0].kind == simulator::EventKind::Companion);
    CHECK(scenario.events[1].on_bravo);
    CHECK(scenario.events[1].kind == simulator::EventKind::Fix);
    CHECK_FALSE(scenario.events[2].on_bravo);
    CHECK(scenario.events[3].kind == simulator::EventKind::BravoLink);
    CHECK(scenario.events[3].value == 0);
}

TEST_CASE(
    "paired scenario: a Bravo event or a link outside a pair, or one Bravo cannot take, "
    "refuses the file") {
    CHECK_FALSE(parses("{\"events\":[{\"at_ms\":0,\"unit\":\"bravo\",\"fix\":0}]}"));
    CHECK_FALSE(parses("{\"product\":\"bravo\",\"events\":[{\"at_ms\":0,\"bravo_link\":0}]}"));
    CHECK_FALSE(
        parses("{\"product\":\"paired\",\"events\":[{\"at_ms\":0,\"unit\":\"bravo\",\"pps\":0}]}"));
    CHECK_FALSE(
        parses("{\"product\":\"paired\",\"events\":[{\"at_ms\":0,\"unit\":\"go\",\"fix\":0}]}"));
}

TEST_CASE("paired scenario: the committed files replay with their expectations met") {
    for (const char* file :
         {"scenarios/paired.json", "scenarios/paired_link_lost.json",
          "scenarios/paired_bravo_without_fix.json", "scenarios/paired_quiet_sky.json",
          "scenarios/paired_dense_sky.json"}) {
        CAPTURE(file);
        Paired paired(file);
        CHECK(paired.simulator.scenario().product == simulator::Product::Paired);
        paired.run_until(paired.simulator.scenario().duration_ms);
        CHECK(paired.simulator.world().failures() == 0);
        CHECK(paired.simulator.product().bravo().refused() == 0);
    }
}

TEST_CASE("paired scenario: Bravo flies the Go's own-ship, and hears the sky the Go flies in") {
    Paired paired("scenarios/paired.json");
    paired.run_until(10000);
    const model::Ownship& go = paired.go().own;
    const model::Ownship& bravo = paired.simulator.bravo().state().own;
    REQUIRE(go.fix_valid);
    REQUIRE(bravo.fix_valid);
    CHECK(bravo.utc_s == go.utc_s);
    CHECK(std::abs(bravo.latitude_1e7 - go.latitude_1e7) < 10);
    CHECK(std::abs(bravo.longitude_1e7 - go.longitude_1e7) < 10);
    CHECK(std::abs(bravo.altitude_hae_mm - go.altitude_hae_mm) < 100);
    CHECK(paired.simulator.world().adsb_heard() > 0);
    CHECK(paired.simulator.bravo().adsb().assembly().count() == 3);
}

TEST_CASE(
    "paired scenario: the Go shows Bravo's ADS-B as B targets, not the airliner it gates out") {
    Paired paired("scenarios/paired.json");
    paired.run_until(10000);
    CHECK(paired.bravo() == comms::BravoState::Live);
    const traffic::Target* ga = target_of(paired.go(), kGa);
    REQUIRE(ga != nullptr);
    CHECK(ga->observation.source == model::Source::Adsb);
    CHECK(ga->observation.address_mapping_table == 5);
    CHECK(target_of(paired.go(), kAirliner) == nullptr);
    CHECK(paired.reads("BRAVO"));
    CHECK_FALSE(paired.reads_lost());

    paired.simulator.world().tap_pad();
    paired.run_until(12000);
    REQUIRE(paired.simulator.product().screen().page() == go::Page::Nearby);
    CHECK(paired.reads("B 300002", go::kNearbyScale));
}

// ADS-L F.2.3 puts the ICAO address on both systems, and the table ranks a direct reception first.
TEST_CASE("paired scenario: an aircraft on ADS-L and ADS-B shows once on the Go, as ADS-L") {
    Paired paired("scenarios/paired.json");
    paired.run_until(10000);
    const traffic::Target* by_bravo = target_of(paired.simulator.bravo().state(), kDual);
    REQUIRE(by_bravo != nullptr);
    CHECK(by_bravo->observation.source == model::Source::Adsb);
    CHECK(slots_of(paired.go(), kDual) == 1);
    const traffic::Target* by_go = target_of(paired.go(), kDual);
    REQUIRE(by_go != nullptr);
    CHECK(by_go->observation.source == model::Source::AdslDirect);
    CHECK(by_go->observation.address_mapping_table == 5);

    paired.simulator.world().tap_pad();
    paired.run_until(12000);
    CHECK(paired.reads("A 300003", go::kNearbyScale));
    CHECK_FALSE(paired.reads("B 300003", go::kNearbyScale));
}

TEST_CASE("paired scenario: a lost link says BRAVO LOST, keeps the ADS-L target, and comes back") {
    Paired paired("scenarios/paired_link_lost.json");
    paired.run_until(11900);
    CHECK(paired.bravo() == comms::BravoState::Live);
    CHECK(paired.go().traffic.count() == 2);
    CHECK_FALSE(paired.reads_lost());

    paired.run_until(16000);
    CHECK(paired.bravo() == comms::BravoState::LinkDown);
    CHECK_FALSE(paired.simulator.aboard().link().up(simulator::PairedLink::Side::Central));
    CHECK(paired.reads_lost());
    CHECK(paired.reads("LINK DOWN"));

    paired.run_until(25000);
    CHECK(paired.reads_lost());
    CHECK(paired.go().traffic.count() == 1);
    CHECK(target_of(paired.go(), 0x300001) == nullptr);
    const traffic::Target* kept = target_of(paired.go(), 0x300002);
    REQUIRE(kept != nullptr);
    CHECK(kept->observation.source == model::Source::AdslDirect);

    paired.run_until(32000);
    CHECK(paired.bravo() == comms::BravoState::Live);
    CHECK_FALSE(paired.reads_lost());
    CHECK(paired.go().traffic.count() == 2);
}

TEST_CASE("paired scenario: BRAVO LOST shows only where the link is lost") {
    for (const char* file :
         {"scenarios/paired.json", "scenarios/paired_bravo_without_fix.json",
          "scenarios/paired_quiet_sky.json", "scenarios/paired_dense_sky.json"}) {
        CAPTURE(file);
        Paired paired(file);
        bool shown = false;
        // Bravo's first health comes before its first UTC, so the Go reads NoUtc for about a
        // second.
        bool lost_after_boot = false;
        while (paired.now_ms <= paired.simulator.scenario().duration_ms) {
            paired.run_until(paired.now_ms + 100);
            shown = shown || paired.reads_lost();
            lost_after_boot =
                lost_after_boot || (paired.now_ms > 2000 && comms::lost(paired.bravo()));
        }
        CHECK_FALSE(shown);
        CHECK_FALSE(lost_after_boot);
        CHECK(paired.bravo() == comms::BravoState::Live);
    }
}

TEST_CASE("paired scenario: Bravo without a fix gates on the Go, which keeps its B targets") {
    Paired paired("scenarios/paired_bravo_without_fix.json");
    paired.run_until(13000);
    const uint32_t read = paired.simulator.phone().sentences_read();
    paired.run_until(16000);
    REQUIRE_FALSE(paired.simulator.bravo().state().own.fix_valid);
    CHECK(paired.simulator.bravo().state().own.utc_valid);
    CHECK(paired.simulator.bravo()
              .gate()
              .choose(paired.simulator.bravo().state().own, paired.now_ms)
              .reference == comms::ExportReference::Go);
    CHECK(paired.bravo() == comms::BravoState::Live);
    for (const uint32_t address : {0x300001u, 0x300002u}) {
        const traffic::Target* target = target_of(paired.go(), address);
        REQUIRE(target != nullptr);
        CHECK(target->observation.source == model::Source::Adsb);
        CHECK(paired.now_ms - target->observation.at_ms < 2000);
    }
    CHECK(paired.bravo_sentences_since(read, "$PFLAA").empty());
    const std::vector<std::string> pflau = paired.bravo_sentences_since(read, "$PFLAU");
    REQUIRE(pflau.size() >= 2);
    for (const std::string& sentence : pflau) CHECK(sentence.rfind("$PFLAU,2,0,0,", 0) == 0);
}

TEST_CASE("paired scenario: a quiet sky is a live Bravo with nothing to show") {
    Paired paired("scenarios/paired_quiet_sky.json");
    paired.run_until(20000);
    CHECK(paired.bravo() == comms::BravoState::Live);
    CHECK(paired.go().traffic.count() == 0);
    CHECK(paired.reads("BRAVO"));
    CHECK(paired.simulator.product().bravo().unplaced() == 0);
}

TEST_CASE("paired scenario: a dense sky reaches the Go through Bravo, all of it ADS-B") {
    Paired paired("scenarios/paired_dense_sky.json");
    paired.run_until(20000);
    CHECK(paired.simulator.bravo().adsb().assembly().count() == 30);
    CHECK(paired.go().traffic.count() >= 8);
    for (int i = 0; i < traffic::TrafficTable::kMaxTargets; i++) {
        const traffic::Target* target = paired.go().traffic.at(i);
        if (target != nullptr && target->used)
            CHECK(target->observation.source == model::Source::Adsb);
    }
}
