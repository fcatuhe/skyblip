// Bravo keeps its buzzer and motor silent while its pinned Go+ takes the alarm, and only then
// (#179).
#include <cstdint>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"
#include "test/support/log_transfer.h"
#include "test/support/paired_link.h"
#include "test/support/registry_frames.h"

using namespace skyblip;

namespace {

constexpr uint32_t kAircraft = 0x3C6586;
constexpr uint32_t kSpeedMmS = 25000;
constexpr uint32_t kStepMs = 10;
constexpr uint16_t kPhoneSession = 3;
constexpr events::BluetoothAddress kStranger{1, {0x01, 0x02, 0x03, 0x04, 0x05, 0xC6}};

std::string sentence_field(const std::string& sentence, int index) {
    size_t at = 0;
    for (int i = 0; i < index; i++) at = sentence.find(',', at) + 1;
    return sentence.substr(at, sentence.find_first_of(",*", at) - at);
}

std::string claim_json(bool requested, bool paired = true) {
    return complete_registry_frame(
        std::string("{\"command\":\"peer_claim\",\"boot_epoch\":7,\"alarm_requested\":") +
        (requested ? "true" : "false") + ",\"paired\":" + (paired ? "true" : "false") + "}");
}

enum class Claim { None, Plain, Requesting };

struct Pinned {
    BravoRig rig{};
    PairedLink go{rig.platform.link()};
    PairedLink* stranger{nullptr};
    std::vector<std::string> to_go{};
    std::vector<std::string> to_stranger{};

    Pinned() {
        REQUIRE(rig.setup() == Status::Ok);
        rig.product.settings().peer = bravo::pin_of(PairedLink::kCentralAddress);
        REQUIRE(go.connect());
    }

    void run_for(uint32_t span_ms) {
        for (uint32_t stepped = 0; stepped < span_ms; stepped += kStepMs) {
            rig.step();
            go.pump();
            if (stranger != nullptr) stranger->pump();
            rig.now_ms += kStepMs;
            for (std::string frame; go.take(frame);) to_go.push_back(link_text(frame));
            if (stranger != nullptr)
                for (std::string frame; stranger->take(frame);)
                    to_stranger.push_back(link_text(frame));
        }
    }

    void claim(bool requested) { REQUIRE(go.write(claim_json(requested)) == Status::Ok); }

    // One second of flight north: Bravo's fix, the Go's claim if it sends one, the passes after.
    void second(Claim kind) {
        rig.push_fix(kSpeedMmS);
        if (kind != Claim::None) claim(kind == Claim::Requesting);
        run_for(1000);
        rig.utc_offset_s++;
    }

    void seconds(uint32_t count, Claim kind) {
        for (uint32_t i = 0; i < count; i++) second(kind);
    }

    // An aircraft ahead, close enough that Bravo grades it an advisory.
    void advisory(Claim kind) {
        rig.hear_aircraft(kAircraft, 800);
        second(kind);
        REQUIRE(rig.state().alarm_undismissed == traffic::Level::Advisory);
    }

    uint32_t tones() { return rig.platform.annunciator().tone_commands(); }
    uint32_t motor_starts() { return rig.platform.annunciator().motor_starts(); }

    static std::string last_health(const std::vector<std::string>& frames) {
        for (auto it = frames.rbegin(); it != frames.rend(); ++it)
            if (field(*it, "command") == "health") return *it;
        return std::string();
    }

    std::string granted() { return field(last_health(to_go), "alarm_granted"); }
};

}  // namespace

TEST_CASE("bravo silence: a fresh requesting claim from its Go silences the buzzer and the motor") {
    Pinned pinned;
    pinned.rig.raise_link(kPhoneSession);
    pinned.seconds(20, Claim::Requesting);
    const uint32_t tones = pinned.tones();
    const uint32_t motor_starts = pinned.motor_starts();
    pinned.advisory(Claim::Requesting);
    pinned.seconds(2, Claim::Requesting);

    CHECK(pinned.tones() == tones);
    CHECK(pinned.motor_starts() == motor_starts);
    CHECK(pinned.rig.state().alarm_granted);
    CHECK(pinned.granted() == "true");
    // FLARM's $PFLAU carries the alarm level fifth: the phone's own alarm is not silenced.
    CHECK(sentence_field(pinned.rig.last_of("$PFLAU"), 5) != "0");
}

TEST_CASE("bravo silence: Bravo is loud from boot, and a claim that does not request keeps it so") {
    Pinned pinned;
    pinned.seconds(20, Claim::None);
    uint32_t tones = pinned.tones();
    pinned.advisory(Claim::None);
    CHECK(pinned.tones() > tones);
    CHECK_FALSE(pinned.rig.state().alarm_granted);

    pinned.seconds(3, Claim::Plain);
    tones = pinned.tones();
    pinned.rig.hear_aircraft(0x4CA2D1, 700);
    pinned.seconds(2, Claim::Plain);
    CHECK(pinned.tones() > tones);
    CHECK_FALSE(pinned.rig.state().alarm_granted);
    CHECK(pinned.granted() == "false");
}

TEST_CASE("bravo silence: Bravo sounds at once when its Go's link drops") {
    Pinned pinned;
    pinned.seconds(20, Claim::Requesting);
    const uint32_t tones = pinned.tones();
    pinned.advisory(Claim::Requesting);
    REQUIRE(pinned.tones() == tones);

    pinned.go.drop();
    pinned.run_for(100);
    CHECK(pinned.tones() > tones);
    CHECK_FALSE(pinned.rig.state().alarm_granted);
}

TEST_CASE("bravo silence: Bravo sounds at once on a claim that stops requesting") {
    Pinned pinned;
    pinned.seconds(20, Claim::Requesting);
    const uint32_t tones = pinned.tones();
    pinned.advisory(Claim::Requesting);
    REQUIRE(pinned.tones() == tones);

    pinned.claim(false);
    pinned.run_for(100);
    CHECK(pinned.tones() > tones);
    CHECK(pinned.granted() == "false");
}

TEST_CASE("bravo silence: Bravo sounds at once when its Go unpairs") {
    Pinned pinned;
    pinned.seconds(20, Claim::Requesting);
    const uint32_t tones = pinned.tones();
    pinned.advisory(Claim::Requesting);
    REQUIRE(pinned.tones() == tones);

    REQUIRE(pinned.go.write(claim_json(true, false)) == Status::Ok);
    pinned.run_for(100);
    CHECK_FALSE(pinned.rig.product.peer().paired());
    CHECK(pinned.tones() > tones);
    CHECK_FALSE(pinned.rig.state().alarm_granted);
}

// One lost claim keeps Bravo silent, and three claim periods without one give the alarm back.
TEST_CASE("bravo silence: claims that stop on a link still up give Bravo its alarm back") {
    Pinned pinned;
    pinned.seconds(20, Claim::Requesting);
    const uint32_t tones = pinned.tones();
    pinned.advisory(Claim::Requesting);
    REQUIRE(pinned.tones() == tones);

    pinned.run_for(bravo::PeerService::kAlarmRequestMaxAgeMs - 1000 - 100);
    CHECK(pinned.tones() == tones);
    CHECK(pinned.rig.state().alarm_granted);
    pinned.run_for(200);
    CHECK(pinned.tones() > tones);
    CHECK_FALSE(pinned.rig.state().alarm_granted);
    pinned.run_for(1000);
    CHECK(pinned.granted() == "false");
}

TEST_CASE("bravo silence: after giving the alarm back Bravo sounds by its own settings") {
    Pinned pinned;
    pinned.rig.product.settings().alarm_volume = bravo::AlarmVolume::Soft;
    pinned.seconds(20, Claim::Requesting);
    pinned.advisory(Claim::Requesting);
    REQUIRE_FALSE(pinned.rig.platform.annunciator().sounding());

    pinned.claim(false);
    pinned.run_for(30);
    CHECK(pinned.rig.platform.annunciator().sounding());
    CHECK(pinned.rig.platform.annunciator().loudness() == ports::Loudness::Soft);
}

TEST_CASE("bravo silence: a requesting claim from a Go Bravo has not pinned silences nothing") {
    Pinned pinned;
    PairedLink stranger{pinned.rig.platform.link(), 2, kStranger};
    pinned.stranger = &stranger;
    REQUIRE(stranger.connect());
    for (int i = 0; i < 20; i++) {
        REQUIRE(stranger.write(claim_json(true)) == Status::Ok);
        pinned.second(Claim::Plain);
    }
    const uint32_t tones = pinned.tones();
    REQUIRE(stranger.write(claim_json(true)) == Status::Ok);
    pinned.advisory(Claim::Plain);

    CHECK(pinned.tones() > tones);
    CHECK_FALSE(pinned.rig.state().alarm_granted);
    CHECK(field(Pinned::last_health(pinned.to_stranger), "alarm_granted") == "false");

    pinned.seconds(2, Claim::Requesting);
    CHECK(pinned.rig.state().alarm_granted);
    CHECK(pinned.granted() == "true");
    CHECK(field(Pinned::last_health(pinned.to_stranger), "alarm_granted") == "false");
}
