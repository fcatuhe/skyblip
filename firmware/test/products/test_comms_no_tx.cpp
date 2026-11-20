// The status push that says why a Go does not transmit, as its glass says it beside NO TX.
#include <string>

#include "core/comms/config.h"
#include "core/events/link.h"
#include "core/timing/transmit_gate.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/link.h"
#include "products/skyblip_go/settings.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/config_frame.h"

using namespace skyblip;
using namespace skyblip::comms;

// A page says why the Go stopped transmitting as the glass does, without polling for it.
TEST_CASE(
    "comms: status pushes the NO TX reason when it changes, and drops it once the Go transmits") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation, kNoOwnTransponder);
    ConfigService config(link, store);
    config.on_link_up(events::LinkUp{1, 200, link.connection(1)});
    link.clear();

    config.set_no_tx(timing::NoTx::None);
    CHECK(link.sent.empty());
    config.set_no_tx(timing::NoTx::Radio);
    REQUIRE(link.sent.size() == 1);
    CHECK(link_text(link.last().bytes).find("\"no_tx\":\"radio\"") != std::string::npos);
    config.set_no_tx(timing::NoTx::Radio);
    CHECK(link.sent.size() == 1);
    config.set_no_tx(timing::NoTx::Gnss);
    REQUIRE(link.sent.size() == 2);
    CHECK(link_text(link.last().bytes).find("\"no_tx\":\"gnss\"") != std::string::npos);
    config.set_no_tx(timing::NoTx::None);
    REQUIRE(link.sent.size() == 3);
    CHECK(link_text(link.last().bytes).find("no_tx") == std::string::npos);
}
