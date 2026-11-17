// Harness, not a test: both rings on one host flash, with a link to answer log commands on.
#ifndef SKYBLIP_TEST_SUPPORT_STORE_RIG_H
#define SKYBLIP_TEST_SUPPORT_STORE_RIG_H

#include <cstdint>
#include <string>

#include "doctest/doctest.h"
#include "hardware/platform/host/identity.h"
#include "hardware/platform/host/random.h"
#include "products/common/services/record_store.h"
#include "products/skyblip_go/flash_window.h"
#include "test/support/product_rig.h"

namespace skyblip {

struct StoreRig {
    platform::host::Clock clock;
    platform::host::Link link;
    platform::host::Identity identity{platform::host::Identity::kSerial};
    platform::host::Random random{};
    ports::NullRoles null;
    ports::FlashRegion& flash;
    ports::Roles roles;
    bus::Bus bus{};
    bus::State state{};
    diagnostics::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    common::RecordPool pool{context, go::book_flash_in_dwell};
    common::RecordStore flights{pool, store::SectorOwner::Flights};
    common::RecordStore capture{pool, store::SectorOwner::Diagnostics};

    explicit StoreRig(ports::FlashRegion& region)
        : flash(region),
          roles{clock,     null.rf,          link,     null.display,         null.key_value,
                region,    null.annunciator, null.dfu, null.die_temperature, null.indicator,
                null.gnss, identity,         random} {
        roles.capabilities = ports::Capability::Storage | ports::Capability::Link;
        link.raise_link(1);
    }

    void open() {
        REQUIRE(flights.open());
        REQUIRE(capture.open());
    }

    std::string listed(common::RecordStore& store, store::SectorOwner owner, uint32_t index) {
        comms::LogRequest request{};
        request.command = comms::LogCommand::List;
        request.store = owner;
        request.peer = events::Peer{link.session_id(), link.connection(link.session_id())};
        request.index_valid = true;
        request.index = index;
        request.understood = true;
        link.clear();
        store.serve(request);
        REQUIRE_FALSE(link.sent.empty());
        return link_text(link.sent.back().bytes);
    }

    // The radio's view of the second, published at the instant a pass began.
    void publish_dwell(uint32_t pass_ms, int phase_ms) {
        timing::ClockState anchored{};
        anchored.utc_valid = true;
        anchored.pps_locked = true;
        state.rf.plan = timing::Scheduler::plan(phase_ms, anchored);
        state.rf.dwell = timing::DwellPhase{pass_ms, phase_ms, true, false};
        clock.set_milliseconds(pass_ms);
    }
};

}  // namespace skyblip

#endif
