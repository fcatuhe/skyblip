// A development Bravo takes a reception trial over the link, with no prompt, and retunes at once.
#include <string>

#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"

using namespace skyblip;

namespace {

constexpr const char* kRunning =
    "{\"command\":\"acknowledgement\",\"acknowledge\":true,\"reason\":\"adsb_trial\"}";

std::string refusal(const char* reason) {
    return std::string("{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"") +
           reason + "\"}";
}

struct TrialRig : BravoRig {
    TrialRig() { product.config_link().config().serve_adsb_trial(); }

    // INFO: fc 13nov26 a status push can follow the answer, so the answer is the acknowledgement
    std::string say(const char* json) {
        const auto& sent = platform.link().sent;
        const size_t before = sent.size();
        send(json);
        run_for(200);
        for (size_t i = before; i < sent.size(); i++) {
            const std::string text = link_text(sent[i].bytes);
            if (sent[i].endpoint == events::Endpoint::Config &&
                text.rfind("{\"command\":\"acknowledgement\"", 0) == 0)
                return text;
        }
        return std::string();
    }
};

}  // namespace

TEST_CASE("adsb trial link: an image that does not serve the trial answers unknown_command") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.send("{\"command\":\"adsb_trial\",\"slot_s\":30,\"thresholds_dbm\":[90]}");
    rig.run_for(200);
    CHECK(rig.last_on(events::Endpoint::Config) == refusal("unknown_command"));
}

TEST_CASE(
    "adsb trial link: a trial is answered at once, and the receiver takes its first profile") {
    TrialRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.say("{\"command\":\"adsb_trial\",\"slot_s\":30,\"thresholds_dbm\":[98],"
                  "\"gain_steps\":[5],\"boosts\":[2],\"front_ends\":[0],\"layouts\":[1]}") ==
          kRunning);
    CHECK(rig.product.button().prompt() == comms::Pending::None);
    CHECK(rig.product.adsb_trial().running());
    const models::Lr2021& part = rig.platform.radio();
    CHECK(part.agc_gain == 5);
    CHECK(part.rx_boost == 2);
    CHECK(part.reg(0xF30150) == 0x75);
    CHECK(part.manchester());
    CHECK(part.listening_for_adsb());

    const uint32_t folded = rig.product.adsb().folded();
    rig.hear_aircraft(0x3C4D5E, 2000);
    CHECK(rig.product.adsb().folded() == folded + 2);
}

TEST_CASE(
    "adsb trial link: a frame with no key but the command stops it, and SoftRF's tuning returns") {
    TrialRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    REQUIRE(rig.say("{\"command\":\"adsb_trial\",\"slot_s\":30,\"thresholds_dbm\":[98],"
                    "\"layouts\":[1]}") == kRunning);
    CHECK(rig.say("{\"command\":\"adsb_trial\"}") == kRunning);
    CHECK_FALSE(rig.product.adsb_trial().running());
    const models::Lr2021& part = rig.platform.radio();
    CHECK_FALSE(part.manchester());
    CHECK(part.reg(0xF30E14) == 0x07000000);
    CHECK(part.listening_for_adsb());
}

TEST_CASE("adsb trial link: a plan it cannot run is refused, and nothing changes") {
    TrialRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    const struct {
        const char* json;
        const char* reason;
    } plans[] = {
        {"{\"command\":\"adsb_trial\",\"thresholds_dbm\":[90]}", "invalid"},
        {"{\"command\":\"adsb_trial\",\"slot_s\":30}", "invalid"},
        {"{\"command\":\"adsb_trial\",\"slot_s\":30,\"thresholds_dbm\":[]}", "invalid"},
        {"{\"command\":\"adsb_trial\",\"slot_s\":30,\"thresholds_dbm\":[90,98],\"layouts\":[1]}",
         "invalid"},
        {"{\"command\":\"adsb_trial\",\"slot_s\":4,\"thresholds_dbm\":[90]}", "out_of_range"},
        {"{\"command\":\"adsb_trial\",\"slot_s\":30,\"thresholds_dbm\":[10]}", "out_of_range"},
        {"{\"command\":\"adsb_trial\",\"slot_s\":30,\"thresholds_dbm\":[90],\"gain_steps\":[14]}",
         "out_of_range"},
        {"{\"command\":\"adsb_trial\",\"slot_s\":30,\"thresholds_dbm\":[90],\"boosts\":[8]}",
         "out_of_range"},
        {"{\"command\":\"adsb_trial\",\"slot_s\":30,\"thresholds_dbm\":[90,90,90,90,90,90,90,90,90]"
         "}",
         "out_of_range"},
    };
    for (const auto& plan : plans) {
        CAPTURE(plan.json);
        CHECK(rig.say(plan.json) == refusal(plan.reason));
        CHECK_FALSE(rig.product.adsb_trial().running());
    }
}
