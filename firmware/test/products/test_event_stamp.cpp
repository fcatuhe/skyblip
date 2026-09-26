// An event is dated by the instant the radio reported, never by the pass that drained it.
#include "core/events/rf.h"
#include "core/model/band.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/air.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/clock.h"
#include "ports/null.h"
#include "products/skyblip_go/services/traffic.h"

using namespace skyblip;

namespace {

// What the modem hands up: chips from the end of the matched window, not from
// the start of the buffer a transmitter filled.
void deliver(const uint8_t* chips, size_t chip_len, uint8_t* out, size_t out_len) {
    const size_t first_bit = protocol::kSharedSyncEndChip;
    for (size_t i = 0; i < out_len; i++) {
        uint8_t byte = 0;
        for (int b = 0; b < 8; b++) {
            const size_t bit = first_bit + i * 8 + static_cast<size_t>(b);
            const bool set = bit < chip_len * 8 && ((chips[bit >> 3] >> (7 - (bit & 7))) & 1u);
            byte = static_cast<uint8_t>((byte << 1) | (set ? 1u : 0u));
        }
        out[i] = byte;
    }
}

constexpr uint32_t kUtc = 1754092800;
constexpr uint64_t kPpsEdgeUs = 40'000'000;
constexpr uint16_t kIntoMs = 437;

struct Rig {
    platform::host::Clock clock;
    ports::NullRoles null;
    ports::Roles roles{
        clock,          null.rf,          null.link, null.display,         null.kv,
        null.log_flash, null.annunciator, null.dfu,  null.die_temperature, null.indicator,
        null.gnss};
    bus::Bus bus{};
    bus::State state{};
    diag::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    go::TrafficService traffic{context, go::kFeatures};

    Rig() {
        roles.capabilities = ports::Capability::Rf;
        state.own.fix_valid = true;
        state.own.utc_valid = true;
        state.own.utc = kUtc;
        state.own.lat_1e7 = 485000000;
        state.own.lon_1e7 = 85000000;
        state.own.alt_mm = 500000;
        state.own.vdop_e2 = 150;
        state.clock.pps_locked = true;
        state.clock.pps_edge_us = kPpsEdgeUs;
        state.clock.utc_s = kUtc;
        REQUIRE(traffic.setup() == Status::Ok);
    }

    void hear(uint32_t addr, uint64_t at_us, uint32_t now_ms) {
        model::OwnState sender = state.own;
        sender.lat_1e7 += 7000;
        protocol::AdslPacket tx;
        protocol::from_own(tx, sender, addr, 6, 4);
        tx.scramble();
        tx.set_crc();

        events::RfEvent event{};
        event.type = events::RfEventType::RxDone;
        event.band = model::Band::M;
        event.at_us = at_us;
        event.rssi_dbm = -80;
        uint8_t chips[protocol::kTxChipBytes] = {0};
        const size_t chip_len = protocol::encode_mband(protocol::kAdslSyncWord, tx.Data,
                                                       protocol::AdslPacket::kDataBytes, chips);
        deliver(chips, chip_len, event.data.data(), protocol::kRxChipBytes);
        event.len = protocol::kRxChipBytes;
        bus.rf.push(event);
        traffic.tick(now_ms);
    }
};

}  // namespace

TEST_CASE("stamp: a reception is dated by its dwell, not by the pass that drained it") {
    Rig rig;
    // Heard 437 ms into the second, drained on a pass 906 ms into it.
    rig.hear(0xC5D804, kPpsEdgeUs + kIntoMs * 1000, 40906);

    REQUIRE(rig.state.air.rx_ok == 1);
    const traffic::Target* target = rig.state.traffic.at(0);
    REQUIRE(target != nullptr);
    CHECK(target->obs.received.at_s == kUtc);
    CHECK(target->obs.received.into_ms == kIntoMs);
    CHECK(target->obs.received.phase_valid);
}
