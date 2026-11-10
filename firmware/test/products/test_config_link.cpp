// The config link's settings blob, held to the link's buffer, since NVS would write any length.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/dfu/update.h"
#include "core/power/cutoff.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/clock.h"
#include "hardware/platform/host/identity.h"
#include "hardware/platform/host/platform.h"
#include "hardware/platform/host/watchdog.h"
#include "ports/null.h"
#include "products/common/services/config_link.h"
#include "products/common/settings_store.h"
#include "products/skyblip_go/settings.h"
#include "products/skyblip_go/settings_store.h"

using namespace skyblip;

namespace {

struct AnyLengthStore : ports::KeyValueStore {
    bool ready() const override { return true; }

    Status read(const char*, uint8_t*, size_t, size_t&) override { return Status::NotFound; }

    Status write(const char* key, const uint8_t* data, size_t bytes) override {
        for (size_t i = 0; i < bytes; i++) checksum ^= data[i];
        written_keys.emplace_back(key);
        writes++;
        return Status::Ok;
    }

    Status erase(const char*) override { return Status::Ok; }

    uint32_t writes{0};
    uint8_t checksum{0};
    std::vector<std::string> written_keys;
};

struct WideSettings : common::SettingsStore {
    explicit WideSettings(size_t reported) : reported_bytes(reported) {}

    void write_fields(cbor::Writer&) const override {}
    void write_default_fields(cbor::Writer&) const override {}
    Status apply(const cbor::Reader&) override { return Status::Ok; }
    Status describe_changes(const cbor::Reader&, char*, int) const override { return Status::Ok; }
    bool pilot_settings_only(const cbor::Reader&) const override { return true; }

    void restore_defaults() override {}
    uint8_t blob_version() const override { return 1; }
    Status adopt(const uint8_t*, size_t) override { return Status::Crc; }
    size_t to_blob(uint8_t*, size_t) const override { return reported_bytes; }

    size_t reported_bytes;
};

struct LinkRig {
    explicit LinkRig(size_t reported_bytes) : settings(reported_bytes) {
        roles.capabilities = ports::Capability::Storage;
    }

    platform::host::Clock clock;
    platform::host::Identity identity{platform::host::Identity::kSerial};
    ports::NullRoles null;
    platform::host::Watchdog watchdog;
    platform::host::Dfu dfu{watchdog};
    AnyLengthStore key_value;
    ports::Roles roles{
        clock,          null.rf,          null.link, null.display,         key_value,
        null.log_flash, null.annunciator, dfu,       null.die_temperature, null.indicator,
        null.gnss,      identity};
    bus::Bus bus{};
    bus::State state{};
    diagnostics::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    power::CutoffMonitor cutoff{};
    WideSettings settings;
    common::ConfigLinkService link{context, settings, cutoff, "skyBlip Test",
                                   dfu::Product::SkyblipGo};

    void change_and_flush() {
        link.load();
        link.config().note_settings_changed();
        link.flush_settings(1000);
    }
};

constexpr size_t kWiderThanTheLinkBuffer = 100;

}  // namespace

TEST_CASE("config link: a blob wider than the link's buffer is a failed write, never written") {
    LinkRig rig{kWiderThanTheLinkBuffer};
    rig.change_and_flush();

    CHECK(rig.key_value.writes == 0);
    CHECK(rig.link.failed_writes() == 1);
}

TEST_CASE("config link: a store that wrote no blob is a refused write, not a saved one") {
    LinkRig rig{0};
    rig.change_and_flush();

    CHECK(rig.key_value.writes == 0);
    CHECK(rig.link.failed_writes() == 1);
}

TEST_CASE("config link: an update records itself but no revert copy wider than the buffer") {
    LinkRig rig{kWiderThanTheLinkBuffer};
    rig.roles.capabilities = ports::Capability::Storage | ports::Capability::Dfu;
    rig.dfu.running_version_valid = true;
    rig.dfu.staged_version_valid = true;
    rig.link.record_update();

    CHECK(rig.key_value.written_keys == std::vector<std::string>{"update"});
    CHECK(rig.link.failed_writes() == 1);
}

TEST_CASE("config link: the Go's store writes no blob into a buffer narrower than the blob") {
    go::Settings settings = go::defaults();
    const go::SettingsStore store{settings, identity::Activation{}};
    uint8_t blob[64]{};

    CHECK(store.to_blob(blob, go::blob_size() - 1) == 0);
    CHECK(store.to_blob(blob, sizeof(blob)) == go::blob_size());
}
