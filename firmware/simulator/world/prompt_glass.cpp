#include "simulator/world/prompt_glass.h"

#include <cstdlib>
#include <cstring>

#include "core/dfu/update.h"
#include "core/util/cbor.h"
#include "core/util/format.h"
#include "products/common/services/config_service.h"
#include "products/skyblip_go/pages/confirm.h"
#include "products/skyblip_go/pages/settings_changes.h"
#include "products/skyblip_go/settings.h"
#include "simulator/world/link_text.h"

namespace skyblip::simulator {

namespace {

struct Named {
    const char* name;
    common::Pending pending;
};

constexpr Named kPrompts[] = {{"set", common::Pending::Set},
                              {"dfu", common::Pending::Dfu},
                              {"recovery", common::Pending::Recovery},
                              {"power_off", common::Pending::PowerOff},
                              {"erase_log", common::Pending::EraseLog},
                              {"gnss_cold_start", common::Pending::GnssColdStart},
                              {"register", common::Pending::Register},
                              {"capture", common::Pending::Capture},
                              {"pair", common::Pending::Pair}};

common::Pending pending_named(const char* name) {
    for (const Named& named : kPrompts)
        if (std::strcmp(named.name, name) == 0) return named.pending;
    return common::Pending::None;
}

constexpr int kFrameBytes = 512;

struct NamedProfile {
    const char* name;
    comms::CaptureProfile profile;
};

constexpr NamedProfile kProfiles[] = {{"full", comms::CaptureProfile::Full},
                                      {"power_run", comms::CaptureProfile::PowerRun},
                                      {"flight_run", comms::CaptureProfile::FlightRun},
                                      {"stop", comms::CaptureProfile::Stop}};

// INFO: fc 15nov26 the page sends the profile it asked for and the flights the dump said it erases
bool capture_detail(const char* argument, char* out) {
    const char* colon = std::strchr(argument, ':');
    if (colon == nullptr) return false;
    const size_t named = static_cast<size_t>(colon - argument);
    for (const NamedProfile& each : kProfiles) {
        if (std::strlen(each.name) != named || std::strncmp(each.name, argument, named) != 0)
            continue;
        char* end = nullptr;
        const unsigned long evicts = std::strtoul(colon + 1, &end, 10);
        if (end == colon + 1 || *end != 0) return false;
        out[common::format_capture_detail(out, each.profile, static_cast<uint32_t>(evicts))] = 0;
        return true;
    }
    return false;
}

// INFO: fc 16nov26 the page names the Bravo as it advertises it, four upper hex digits
bool pair_detail(const char* argument, char* out) {
    constexpr size_t kTagDigits = 4;
    if (std::strlen(argument) != kTagDigits) return false;
    uint16_t tag = 0;
    for (size_t i = 0; i < kTagDigits; i++) {
        const char digit = argument[i];
        const bool decimal = digit >= '0' && digit <= '9';
        const bool letter = digit >= 'A' && digit <= 'F';
        if (!decimal && !letter) return false;
        tag = static_cast<uint16_t>(tag << 4 | (decimal ? digit - '0' : digit - 'A' + 10));
    }
    (void)common::format_pair_detail(out, tag);
    return true;
}

bool applied(go::Settings& settings, const char* json) {
    uint8_t frame[kFrameBytes];
    const int bytes = link_text::frame_from_text(json, frame, kFrameBytes);
    if (bytes <= 0) return false;
    const cbor::Reader reader(ConstByteSpan(frame, static_cast<size_t>(bytes)));
    return go::apply_settings(settings, reader) == Status::Ok;
}

// INFO: fc 17nov26 any pin stands for the pairing, since the alarm row reads only whether there is
// one
constexpr events::BluetoothAddress kAnyBravo{1, {1, 0, 0, 0, 0, 0xC0}};

bool settings_detail(const char* set, const char* settings, PromptUnit unit, char* out,
                     int capacity) {
    go::Settings from = go::defaults();
    if (unit.paired) from.bravo = kAnyBravo;
    if (!applied(from, settings)) return false;
    go::Settings to = from;
    if (!applied(to, set)) return false;
    return go::describe_settings_changes(from, to, out, capacity, unit.sounds) > 0;
}

// INFO: fc 14nov26 a screen reader hears the page's words in reading order, the keys last
void say(char* out, int capacity, const char* title, const char* detail, uint32_t seconds_left) {
    char text[kPromptWordsBytes];
    int written = format_string(text, title);
    written += format_string(text + written, ". ");
    for (const char* at = detail; *at != 0 && written < kPromptWordsBytes - 80; at++) {
        if (*at == '\n')
            written += format_string(text + written, ", ");
        else
            text[written++] = *at;
    }
    if (written > 0 && text[written - 1] != '.') text[written++] = '.';
    if (seconds_left != 0) {
        text[written++] = ' ';
        written += go::format_confirm_countdown(text + written, seconds_left);
        text[written++] = '.';
    }
    written += format_string(text + written, " PUSH TO ");
    written += format_string(text + written, go::kConfirmAllowVerb);
    written += format_string(text + written, ". TOUCH THE PAD TO ");
    written += format_string(text + written, go::kConfirmCancelVerb);
    text[written++] = '.';
    text[written] = 0;
    std::strncpy(out, text, static_cast<size_t>(capacity) - 1);
    out[capacity - 1] = 0;
}

}  // namespace

bool draw_prompt(go::Glass& glass, const char* prompt, const char* argument, const char* settings,
                 uint32_t seconds_left, char* words, int words_capacity, PromptUnit unit) {
    const common::Pending pending = pending_named(prompt);
    if (pending == common::Pending::None) return false;
    char detail[common::kPromptDetailBytes]{};
    if (pending == common::Pending::Set) {
        if (!settings_detail(argument, settings, unit, detail, sizeof(detail))) return false;
    } else if (pending == common::Pending::Dfu) {
        ports::ImageVersion version;
        if (!dfu::parse_version(argument, version)) return false;
        (void)common::format_dfu_detail(detail, sizeof(detail), version);
    } else if (pending == common::Pending::Capture && argument[0] != 0) {
        if (!capture_detail(argument, detail)) return false;
    } else if (pending == common::Pending::Pair) {
        if (!pair_detail(argument, detail)) return false;
    } else {
        std::strncpy(detail, common::pending_detail(pending), sizeof(detail) - 1);
    }
    go::ConfirmSnapshot snapshot;
    snapshot.title = common::pending_title(pending);
    snapshot.detail = detail;
    snapshot.seconds_left = seconds_left;
    go::draw_confirm(glass, snapshot);
    if (words != nullptr && words_capacity > 0)
        say(words, words_capacity, snapshot.title, detail, seconds_left);
    return true;
}

}  // namespace skyblip::simulator
