// The companion connection on the running product: raised by the platform's own
// link, drained by the board onto the bus, read once by the config service.
//
// The first case in this file is a guard, and it exists because the tree shipped
// events::LinkUp, events::LinkDown and two handlers for them with no producer
// anywhere: no board, no platform, no service ever put one on the bus. Every host
// case that needed a link called ConfigService::on_link_up() by hand, so the suite
// was green while a real device never saw a link come up - the battery gauge never
// pushed, and a phone walking out of range left its prompt and its upload window
// standing. Nothing below the services is stubbed here: a case connects the way a
// central does and asserts what a pilot would see.
#include <string>

#include "core/events/link.h"
#include "doctest/doctest.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

void taxi(Rig& rig, uint32_t& t, uint32_t seconds) { rig.seconds(t, seconds, 0, 300); }
void fly(Rig& rig, uint32_t& t, uint32_t seconds) { rig.seconds(t, seconds, 25000, 900); }

int config_frames(Rig& rig) { return rig.platform.link().count_on(events::Endpoint::Config); }

// The last thing the device said on an endpoint, empty when it said nothing.
// A string rather than a pointer so a case that was going to fail fails on the
// assertion rather than on the dereference after it.
std::string last_frame_on(Rig& rig, events::Endpoint endpoint) {
    const auto& sent = rig.platform.link().sent;
    for (auto it = sent.rbegin(); it != sent.rend(); ++it)
        if (it->endpoint == endpoint) return it->bytes;
    return std::string();
}

}  // namespace

// THE GUARD. If the product's link path can never raise a LinkUp, this fails.
//
// It drives the platform's link model, not the service: raise_link() is the same
// comms::LinkSession that Zephyr's connected() callback drives on silicon, and
// the event has to travel platform -> board::poll -> bus.link_events -> the config
// service's one drain before link_up() can be true. Delete any link in that chain
// - the callback, the board's while-pop, the service's drain - and this case goes
// red. It would have caught the original bug because it cannot be written at all
// against a platform with no way to raise a connection, which is exactly what the
// tree had: the only way to make the service believe in a link was to call it.
TEST_CASE("companion link: the product raises LinkUp through its own platform, not by hand") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(rig, t, 3);
    REQUIRE_FALSE(rig.link_up());

    rig.raise_link(0x0042);
    // The platform knows a central is there the moment it connects. The bug this
    // guards was the other half of this pair, and only the other half: the
    // service was never told, because nothing carried it.
    CHECK(rig.platform.link().up());
    rig.run(t, t + 100);
    t += 100;
    CHECK(rig.link_up());
    // Connected is not configuring: nothing is claimed until the app asks.
    CHECK_FALSE(rig.product.config().config().claim().held());
    rig.send("{\"cmd\":\"get\"}");
    rig.run(t, t + 100);
    t += 100;
    CHECK(rig.product.config().config().session() == 0x0042);

    rig.drop_link();
    CHECK_FALSE(rig.platform.link().up());
    rig.run(t, t + 100);
    CHECK_FALSE(rig.link_up());
}

// G1, end to end and on the wire: the launch gate says the state of charge
// reaches the pilot's tablet without it asking. It never did on silicon, because
// the push is gated on a link being up and nothing ever said one was.
TEST_CASE(
    "companion link: a connected tablet's gauge moves without asking, and stops when it goes") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(rig, t, 4);
    rig.platform.link().clear();

    // Nobody is listening yet: the cell can do what it likes and nothing is sent.
    rig.platform.battery().millivolts = 3900;
    taxi(rig, t, 3);
    CHECK(config_frames(rig) == 0);

    rig.raise_link();
    rig.run(t, t + 100);
    t += 100;
    REQUIRE(rig.link_up());
    // A connection on its own is not news about the battery.
    CHECK(config_frames(rig) == 0);

    rig.platform.battery().millivolts = 3600;
    taxi(rig, t, 4);
    const int pushed = config_frames(rig);
    CHECK(pushed >= 1);
    const std::string status = last_frame_on(rig, events::Endpoint::Config);
    CHECK(status.find("\"cmd\":\"status\"") != std::string::npos);
    CHECK(status.find("\"battery_percent\"") != std::string::npos);

    rig.drop_link();
    rig.run(t, t + 100);
    t += 100;
    REQUIRE_FALSE(rig.link_up());

    // A charger going in is the loudest thing that can happen to the gauge, and
    // with nobody there it is silence, not a notification into a dead handle.
    rig.platform.battery().external_power = true;
    rig.platform.battery().millivolts = 4100;
    taxi(rig, t, 5);
    CHECK(config_frames(rig) == pushed);
}

// The one setting no menu can carry, so the link is the only way in: pinned end
// to end because a name nobody can set is a feature nobody has.
TEST_CASE("companion link: a callsign is written by an app and authorised on the glass") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(rig, t, 6);
    REQUIRE(rig.settings().callsign[0] == 0);

    rig.send("{\"cmd\":\"set\",\"callsign\":\"F-JABC\"}");
    rig.run(t, t + 3000);
    t += 3000;
    // Staged, not applied: the glass has the question and nothing is stored yet.
    REQUIRE(rig.product.config().config().pending() == comms::Pending::Set);
    CHECK(rig.settings().callsign[0] == 0);

    rig.double_press(t);
    rig.run(t, t + 1000);
    t += 1000;
    CHECK(std::string(rig.settings().callsign) == "F-JABC");
}

// A write is refused in the air, so a name is set before the aircraft moves.
TEST_CASE("companion link: a callsign written in flight is refused, not staged") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 6);

    rig.send("{\"cmd\":\"set\",\"callsign\":\"F-JABC\"}");
    rig.run(t, t + 3000);
    t += 3000;
    CHECK(rig.product.config().config().pending() == comms::Pending::None);
    rig.double_press(t);
    rig.run(t, t + 200);
    t += 200;
    CHECK(rig.settings().callsign[0] == 0);
}

TEST_CASE("companion link: a connection that drops takes the standing prompt with it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(rig, t, 20);
    REQUIRE(rig.product.config().config().flight_state() == flight::FlightState::OnGround);

    rig.raise_link();
    rig.send("{\"cmd\":\"dfu\"}");
    rig.run(t, t + 3000);
    t += 3000;
    REQUIRE(rig.product.config().config().pending() == comms::Pending::Dfu);
    REQUIRE(rig.product.screen().prompt() == comms::Pending::Dfu);

    // The phone that asked is gone. An authorisation left standing on the glass
    // is one a stranger can walk up to and answer with the button.
    rig.drop_link();
    rig.run(t, t + 200);
    t += 200;
    CHECK(rig.product.config().config().pending() == comms::Pending::None);
    CHECK(rig.product.screen().prompt() == comms::Pending::None);
    CHECK_FALSE(rig.product.config().config().upload_allowed());
}

TEST_CASE("companion link: a dropped connection closes the upload window it opened") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(rig, t, 20);

    rig.raise_link();
    rig.send("{\"cmd\":\"dfu\"}");
    rig.run(t, t + 3000);
    t += 3000;
    REQUIRE(rig.product.config().config().pending() == comms::Pending::Dfu);
    rig.double_press(t);
    rig.run(t, t + 200);
    t += 200;
    REQUIRE(rig.product.config().config().upload_allowed());

    // The window is what the MCUmgr hook consults before it accepts a byte of
    // image, so a phone that walks away with it open leaves the secondary slot
    // writable by anyone in range for the rest of the ten minutes.
    rig.drop_link();
    rig.run(t, t + 200);
    t += 200;
    CHECK_FALSE(rig.product.config().config().upload_allowed());
}

// The log offload keeps no state between round trips - one command, one reply -
// so a link dropping in the middle of one leaves nothing stale behind. The one
// thing an offload can leave standing is the erase prompt, and that is the same
// prompt machine a firmware upload uses, so the same disconnect takes it away.
TEST_CASE("companion link: a link dropped mid-offload leaves the log ready for the next one") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(rig, t, 20);
    rig.seconds(t, 60, 50000, 800);
    taxi(rig, t, 40);
    REQUIRE(rig.product.flight_log().records_written() > 0);

    rig.raise_link(11);
    rig.send_log("{\"cmd\":\"erase\"}");
    rig.run(t, t + 3000);
    t += 3000;
    REQUIRE(rig.product.config().config().pending() == comms::Pending::EraseLog);

    rig.drop_link();
    rig.run(t, t + 200);
    t += 200;
    CHECK(rig.product.config().config().pending() == comms::Pending::None);
    CHECK_FALSE(rig.product.flight_log().erasing());

    // A new phone, and the offload answers it: nothing about the dead session
    // was carried forward.
    rig.platform.link().clear();
    rig.raise_link(12);
    rig.send_log("{\"cmd\":\"list\"}");
    rig.run(t, t + 200);
    t += 200;
    REQUIRE(rig.link_up());
    CHECK(rig.product.config().config().session() == 12);
    CHECK(rig.platform.link().count_on(events::Endpoint::Log) >= 1);
    CHECK(last_frame_on(rig, events::Endpoint::Log).find("\"sessions\"") != std::string::npos);
}

// An iOS central connects and exchanges the MTU afterwards, so the product sees
// a second LinkUp on a session it already has open.
TEST_CASE("companion link: a late MTU exchange is the same phone, not a new one") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(rig, t, 20);

    rig.platform.link().declare_payload_bytes(ports::kMinimumLinkPayload);
    rig.raise_link(5);
    rig.send("{\"cmd\":\"dfu\"}");
    rig.run(t, t + 3000);
    t += 3000;
    REQUIRE(rig.product.config().config().pending() == comms::Pending::Dfu);

    // 182 is an iPhone's usual answer, and it lands after the service is already
    // waiting for a button press. The prompt is the phone's, and it is still the
    // same phone.
    rig.platform.link().declare_payload_bytes(182);
    rig.run(t, t + 200);
    t += 200;
    CHECK(rig.link_up());
    CHECK(rig.product.config().config().session() == 5);
    CHECK(rig.product.config().config().pending() == comms::Pending::Dfu);
}

// The traffic picture is the same picture on every screen, so a tablet on the
// yoke and a phone in a pocket both read it off one device.
TEST_CASE("companion link: two centrals read the same traffic stream") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(rig, t, 3);

    rig.raise_link(1);
    rig.raise_link(2);
    rig.run(t, t + 100);
    t += 100;
    REQUIRE(rig.platform.link().links() == 2);

    rig.platform.link().clear();
    taxi(rig, t, 3);
    // One frame formatted once, put on every subscribed connection by the
    // platform: what the host model records is the broadcast, session 0.
    CHECK(rig.platform.link().count_to(0, events::Endpoint::Nmea) >= 3);
}

// Configuration is not broadcast: two apps writing settings is two apps
// disagreeing about what the device stores.
TEST_CASE("companion link: the first app to ask holds config, and the second is told who has it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(rig, t, 20);

    rig.raise_link(1);
    rig.raise_link(2);
    rig.send_from(1, "{\"cmd\":\"get\"}");
    rig.run(t, t + 200);
    t += 200;
    REQUIRE(rig.product.config().config().session() == 1);
    REQUIRE(rig.platform.link().count_to(1, events::Endpoint::Config) == 1);

    rig.platform.link().clear();
    rig.send_from(2, "{\"cmd\":\"dfu\"}");
    rig.run(t, t + 200);
    t += 200;
    // Refused, with a reason and to the app that asked. Not silence, which a page
    // cannot tell from a device that died.
    const std::string refusal = rig.last_on(events::Endpoint::Config);
    CHECK(refusal.find("\"reason\":\"claimed\"") != std::string::npos);
    CHECK(rig.platform.link().count_to(2, events::Endpoint::Config) == 1);
    CHECK(rig.product.config().config().pending() == comms::Pending::None);
}

// A central that never exchanged its MTU once capped every reply, and silenced the app that asked.
TEST_CASE("companion link: a reply is sized for the app that asked, not the narrowest link") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(rig, t, 20);

    rig.raise_link(1);
    rig.raise_link(2);
    rig.platform.link().declare_payload_bytes(2, ports::kMinimumLinkPayload);
    rig.send_from(1, "{\"cmd\":\"get\"}");
    rig.run(t, t + 200);
    REQUIRE(rig.platform.link().count_to(1, events::Endpoint::Config) == 1);
    CHECK(rig.last_on(events::Endpoint::Config).size() > ports::kMinimumLinkPayload);
    CHECK(rig.platform.link().refused_oversize == 0);
}

// The prompt on the glass belongs to the app that raised it, so the other phone
// walking out of range must not answer it or take it away.
TEST_CASE("companion link: a second app leaving does not cancel the holder's prompt") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    taxi(rig, t, 20);

    rig.raise_link(1);
    rig.raise_link(2);
    rig.send_from(1, "{\"cmd\":\"dfu\"}");
    rig.run(t, t + 200);
    t += 200;
    REQUIRE(rig.product.config().config().pending() == comms::Pending::Dfu);

    rig.drop_link(2);
    rig.run(t, t + 200);
    t += 200;
    CHECK(rig.product.config().config().pending() == comms::Pending::Dfu);
    CHECK(rig.product.config().config().session() == 1);

    // And when the holder leaves, the prompt goes with it and the claim is free
    // for the phone that is still connected.
    rig.drop_link(1);
    rig.run(t, t + 200);
    t += 200;
    CHECK(rig.product.config().config().pending() == comms::Pending::None);
    CHECK_FALSE(rig.product.config().config().claim().held());
    CHECK_FALSE(rig.link_up());

    rig.raise_link(2);
    rig.run(t, t + 100);
    t += 100;
    rig.send_from(2, "{\"cmd\":\"get\"}");
    rig.run(t, t + 200);
    CHECK(rig.product.config().config().session() == 2);
}
