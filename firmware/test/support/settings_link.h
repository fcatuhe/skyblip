// Harness, not a test: a settings patch and the config reply, named in JSON as an app has them.
#ifndef SKYBLIP_TEST_SUPPORT_SETTINGS_LINK_H
#define SKYBLIP_TEST_SUPPORT_SETTINGS_LINK_H

#include <cstdint>
#include <string>

#include "core/comms/link_registry.h"
#include "core/settings/address.h"
#include "core/util/cbor.h"
#include "products/skyblip_go/settings.h"
#include "test/support/config_frame.h"

namespace skyblip {

inline Status apply_patch(go::Settings& settings, const char* patch) {
    const std::string set = std::string("{\"command\":\"set\",") + (patch + 1);
    const events::RxFrame frame = config_frame(set.c_str());
    return go::apply_settings(settings,
                              cbor::Reader(ConstByteSpan(frame.data.data(), frame.bytes)));
}

inline std::string config_fields(const go::Settings& settings, settings::AirAddress address) {
    uint8_t frame[256];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Config);
    go::write_config_fields(writer, settings, address);
    const int bytes = writer.finish();
    return link_text(std::string(reinterpret_cast<const char*>(frame), static_cast<size_t>(bytes)));
}

}  // namespace skyblip

#endif
