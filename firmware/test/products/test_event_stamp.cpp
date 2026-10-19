// An event is dated by the instant the radio reported, never by the pass that drained it.
#include "core/events/rf.h"
#include "core/model/band.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/air.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/clock.h"
#include "hardware/platform/host/identity.h"
#include "ports/null.h"
#include "products/skyblip_go/services/traffic.h"

using namespace skyblip;

namespace {

// What the modem hands up: chips from the end of the matched window, not from
// the start of the buffer a transmitter filled.
void deliver(const uint8_t* chips, size_t chip_length, uint8_t* out, size_t out_length) {
    const size_t first_bit = protocol::kSharedSyncEndChip;
    for (size_t i = 0; i < out_length; i++) {
        uint8_t byte = 0;
        for (int b = 0; b < 8; b++) {
            const size_t bit = first_bit + i * 8 + static_cast<size_t>(b);
            const bool set = bit < chip_length * 8 && ((chips[bit >> 3] >> (7 - (bit & 7))) & 1u);
            byte = static_cast<uint8_t>((byte << 1) | (set ? 1u : 0u));
        }
        out[i] = byte;
    }
}

constexpr uint32_t kUtcS = 1754092800;
constexpr uint64_t kPpsEdgeUs = 40'000'000;
constexpr uint16_t kIntoMs = 437;

struct StampRig {
    platform::host::Clock clock;
    platform::host::Identity identity{platform::host::Identity::kSerial};
    ports::NullRoles null;
    ports::Roles roles{
        clock,          null.rf,          null.link, null.display,         null.key_value,
        null.log_flash, null.annunciator, null.dfu,  null.die_temperature, null.indicator,
        null.gnss,      identity};
    bus::Bus bus{};
    bus::State state{};
    diagnostics::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    go::TrafficService traffic{context, go::kFeatures};

    StampRig() {
        roles.capabilities = ports::Capability::Rf;
        state.own.fix_valid = true;
        state.own.utc_valid = true;
        state.own.utc_s = kUtcS;
        state.own.latitude_1e7 = 485000000;
        state.own.longitude_1e7 = 85000000;
        state.own.altitude_hae_mm = 500000;
        state.own.vdop_e2 = 150;
        state.clock.pps_locked = true;
        state.clock.pps_edge_us = kPpsEdgeUs;
        state.clock.utc_s = kUtcS;
        REQUIRE(traffic.setup() == Status::Ok);
    }

    void hear(uint32_t address, uint64_t at_us, uint32_t now_ms) {
        model::Ownship sender = state.own;
        sender.latitude_1e7 += 7000;
        protocol::AdslPacket tx;
        protocol::from_own(tx, sender, address, 6, 4);
        tx.scramble();
        tx.set_crc();

        events::RfEvent event{};
        event.type = events::RfEventType::RxDone;
        event.band = model::Band::M;
        event.at_us = at_us;
        event.rssi_dbm = -80;
        uint8_t chips[protocol::kTxChipBytes] = {0};
        const size_t chip_length = protocol::encode_m_band(protocol::kAdslSyncWord, tx.data,
                                                           protocol::AdslPacket::kDataBytes, chips);
        deliver(chips, chip_length, event.data.data(), protocol::kRxChipBytes);
        event.bytes = protocol::kRxChipBytes;
        bus.rf.push(event);
        traffic.tick(now_ms);
    }
};

}  // namespace

TEST_CASE("stamp: a reception is dated by its dwell, not by the pass that drained it") {
    StampRig rig;
    // Heard 437 ms into the second, drained on a pass 906 ms into it.
    rig.hear(0xC5D804, kPpsEdgeUs + kIntoMs * 1000, 40906);

    REQUIRE(rig.state.air.rx_ok == 1);
    const traffic::Target* target = rig.state.traffic.at(0);
    REQUIRE(target != nullptr);
    CHECK(target->observation.received.at_s == kUtcS);
    CHECK(target->observation.received.into_ms == kIntoMs);
    CHECK(target->observation.received.phase_valid);
}
