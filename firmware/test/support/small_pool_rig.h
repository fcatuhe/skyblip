// Harness, not a test: a capture on a partition small enough to fill, labelled sector by sector.
#ifndef SKYBLIP_TEST_SUPPORT_SMALL_POOL_RIG_H
#define SKYBLIP_TEST_SUPPORT_SMALL_POOL_RIG_H

#include <cstdint>

#include "boards/lilygo/t_echo_plus/log_flash.h"
#include "core/diagnostics/payload.h"
#include "core/store/sector.h"
#include "doctest/doctest.h"
#include "products/common/services/capture.h"
#include "products/skyblip_go/capture_settings.h"
#include "products/skyblip_go/flash_window.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/capture_rig.h"
#include "test/support/diagnostics_read.h"

namespace skyblip {

inline void label_sector(platform::host::FlashRegion& flash, uint32_t sector,
                         store::SectorOwner owner, uint32_t sequence, uint32_t session) {
    store::SectorHeader header{};
    header.owner = owner;
    header.sequence = sequence;
    header.session_id = session;
    header.record_bytes = static_cast<uint8_t>(diagnostics::kRecordBytes);
    uint8_t raw[store::kSectorHeaderBytes];
    store::encode_sector_header(header, raw);
    REQUIRE(
        is_ok(flash.write(sector * platform::host::FlashRegion::kSectorBytes, raw, sizeof(raw))));
}

// A partition the floor swallows whole, the one shape the allocator can refuse a capture in.
struct SmallPoolRig {
    static constexpr uint32_t kSectors =
        store::flights_floor_sectors(flight::log_seconds_per_sector(flight::kLogSlotsPerSector)) +
        1;

    platform::host::Clock clock;
    platform::host::Link link;
    platform::host::FlashRegion flash;
    ports::NullRoles null;
    platform::host::Identity identity{platform::host::Identity::kSerial};
    platform::host::Random random{};
    ports::Roles roles{clock,     null.rf,          link,     null.display,         null.key_value,
                       flash,     null.annunciator, null.dfu, null.die_temperature, null.indicator,
                       null.gnss, identity,         random};
    bus::Bus bus{};
    bus::State state{};
    diagnostics::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    go::Settings settings{};
    go::SettingsStore store{settings, state.activation, state.own_transponder, state.aboard};
    common::ConfigService config{link, store};
    store::SectorSlots<boards::t_echo_plus::kLogSectors> log_sectors{};
    common::RecordPool pool{context, go::book_flash_in_dwell, log_sectors.table()};
    common::RecordStore flights{pool, store::SectorOwner::Flights};
    common::RecordStore capture_store{pool, store::SectorOwner::Diagnostics};
    go::CaptureSettings capture_settings{settings};
    common::CaptureService capture{context, capture_store, flights, capture_settings, config};

    explicit SmallPoolRig(uint32_t sectors = kSectors) : flash(sectors) {
        roles.capabilities = ports::Capability::Storage | ports::Capability::Link;
        state.own.utc_valid = true;
        state.own.utc_s = kBaseUtcS;
        link.raise_link(1);
    }

    void label(uint32_t sector, store::SectorOwner owner, uint32_t sequence, uint32_t session) {
        label_sector(flash, sector, owner, sequence, session);
    }

    void setup() {
        flights.open();
        REQUIRE(capture.setup() == Status::Ok);
    }

    void fill_flights_to_the_floor() {
        const uint32_t floor_sectors = store::flights_floor_sectors(
            flight::log_seconds_per_sector(flight::kLogSlotsPerSector));
        for (uint32_t sector = 0; sector < floor_sectors; sector++)
            label(sector, store::SectorOwner::Flights, sector + 1, 4000);
    }

    uint32_t now_ms{0};

    uint32_t feed_until_stopped(uint32_t records) {
        uint32_t pushed = 0;
        while (pushed < records && capture.capturing()) {
            for (int i = 0; i < diagnostics::Recorder::kMaxRecords && pushed < records; i++)
                if (recorder.record(bench_record(kBaseUtcS + pushed))) pushed++;
            capture.tick(now_ms);
            now_ms += 50;
        }
        capture.tick(now_ms);
        return pushed;
    }
};

struct CaptureWalk {
    int gaps{0};
    uint32_t biggest_gap{0};
    int boots_after_gap{0};
    int configs_after_gap{0};
};

inline CaptureWalk walk_sectors(SmallPoolRig& rig, uint32_t session) {
    CaptureWalk walk{};
    uint32_t index = 0;
    uint32_t sector = 0;
    while (rig.pool.allocator().session_sector(store::SectorOwner::Diagnostics, session, index,
                                               sector)) {
        for (uint32_t slot = 0; slot < rig.pool.slots_per_sector(); slot++) {
            uint8_t raw[diagnostics::kRecordBytes];
            if (!rig.pool.read_slot(sector, slot, raw)) break;
            diagnostics::Record record{};
            if (diagnostics::decode_record(raw, record) != Status::Ok) break;
            if (record.type == diagnostics::Type::Gap) {
                diagnostics::Gap gap{};
                REQUIRE(diagnostics::read(record, gap));
                walk.gaps++;
                if (gap.dropped > walk.biggest_gap) walk.biggest_gap = gap.dropped;
            }
            if (walk.gaps > 0 && record.type == diagnostics::Type::Boot) walk.boots_after_gap++;
            if (walk.gaps > 0 && record.type == diagnostics::Type::Config) walk.configs_after_gap++;
        }
        index++;
    }
    return walk;
}

// Slowly enough that the RAM ring never overflows, so every hole on the flash is the store's.
inline void feed_gently(SmallPoolRig& rig, uint32_t records) {
    uint32_t pushed = 0;
    while (pushed < records && rig.capture.capturing()) {
        for (int i = 0; i < 8 && pushed < records; i++)
            if (rig.recorder.record(bench_record(kBaseUtcS + pushed))) pushed++;
        rig.capture.tick(rig.now_ms);
        rig.now_ms += 50;
    }
    rig.capture.tick(rig.now_ms);
}

}  // namespace skyblip

#endif
