// A settings store that fails to read was taken for a fresh unit, and nobody heard of it.
#include <cstdlib>
#include <string>

#include "core/settings/blob.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/self_test.h"
#include "products/skyblip_go/services/diagnostics.h"
#include "test/support/bravo_rig.h"
#include "test/support/product_rig.h"
#include "test/support/settings_writes.h"

using namespace skyblip;

namespace {

const go::SelfTestPart& storage_row(Rig& rig) {
    for (int i = 0; i < go::kSelfTestPartCount; i++)
        if (std::string(rig.product.self_test().parts[i].name) == "STORAGE")
            return rig.product.self_test().parts[i];
    return rig.product.self_test().parts[0];
}

std::string update_frame(const platform::host::Link& link) {
    for (const auto& frame : link.sent) {
        const std::string text = link_text(frame.bytes);
        if (text.find("\"command\":\"update\"") != std::string::npos) return text;
    }
    return "";
}

long failed_writes_in_the_dump(const platform::host::Link& link) {
    const std::string key = "\"failed_writes\":";
    for (const auto& frame : link.sent) {
        const std::string text = link_text(frame.bytes);
        if (text.find("\"command\":\"diagnostics\"") == std::string::npos) continue;
        const size_t at = text.find(key);
        if (at != std::string::npos)
            return std::strtol(text.c_str() + at + key.size(), nullptr, 10);
    }
    return -1;
}

}  // namespace

TEST_CASE(
    "settings store: flash that fails to read starts from defaults, on the self test and "
    "to a phone") {
    Rig rig;
    rig.platform.key_value().broken = true;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.product.config_link().settings_fallback() == settings::Fallback::Defaults);
    CHECK(std::string(storage_row(rig).detail) == go::kStorageOnDefaults);

    rig.raise_link();
    rig.run(0, 200);
    CHECK(update_frame(rig.platform.link()).find("\"settings\":\"defaults\"") != std::string::npos);
}

TEST_CASE("settings store: a store that holds no settings yet is a fresh unit, not a fault") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.product.config_link().settings_fallback() == settings::Fallback::None);
    CHECK(std::string(storage_row(rig).detail) != go::kStorageOnDefaults);
}

TEST_CASE("settings store: Bravo on flash that fails to read tells a phone it runs on defaults") {
    BravoRig rig;
    rig.platform.key_value().broken = true;
    REQUIRE(rig.setup() == Status::Ok);
    rig.raise_link();
    rig.run_for(200);
    CHECK(update_frame(rig.platform.link()).find("\"settings\":\"defaults\"") != std::string::npos);
}

// A change the flash refused was counted in failed_writes(), which nothing showed a pilot or
// support.
TEST_CASE("settings store: a settings write the flash refuses is counted in the diagnostics dump") {
    Rig rig;
    rig.platform.key_value().broken = true;
    REQUIRE(rig.setup() == Status::Ok);
    go::DiagnosticsCollector<Go> collector;
    rig.world = [&](uint32_t at_ms) { collector.step(rig.product, at_ms); };
    settings_writes::change_alarm(rig, go::Alarm::Soft);
    rig.run_for(5000);
    REQUIRE(rig.product.config_link().failed_writes() > 0);

    rig.send("{\"command\":\"diagnostics\"}");
    rig.run_for(200);
    CHECK(failed_writes_in_the_dump(rig.platform.link()) > 0);
}

TEST_CASE("settings store: a unit whose writes all land says failed_writes 0 in the dump") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    go::DiagnosticsCollector<Go> collector;
    rig.world = [&](uint32_t at_ms) { collector.step(rig.product, at_ms); };
    settings_writes::change_alarm(rig, go::Alarm::Soft);
    rig.run_for(5000);
    REQUIRE(rig.platform.key_value().writes() > 0);

    rig.send("{\"command\":\"diagnostics\"}");
    rig.run_for(200);
    CHECK(failed_writes_in_the_dump(rig.platform.link()) == 0);
}

TEST_CASE("settings store: flash that fails is a write that failed, never a full store") {
    platform::host::KeyValueStore store;
    store.broken = true;
    const uint8_t blob[] = {1, 2, 3};
    uint8_t out[sizeof(blob)]{};
    size_t out_bytes = 0;
    CHECK(store.write("settings", blob, sizeof(blob)) == Status::Down);
    CHECK(store.read("settings", out, sizeof(out), out_bytes) == Status::Down);
    CHECK(store.erase("settings") == Status::Down);
}
