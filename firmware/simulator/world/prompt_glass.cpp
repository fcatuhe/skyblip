#include "simulator/world/prompt_glass.h"

#include <cstring>

#include "core/comms/config.h"
#include "core/dfu/update.h"
#include "core/util/cbor.h"
#include "core/util/format.h"
#include "products/skyblip_go/pages/confirm.h"
#include "products/skyblip_go/pages/settings_changes.h"
#include "products/skyblip_go/settings.h"
#include "simulator/world/link_text.h"

namespace skyblip::simulator {

namespace {

struct Named {
    const char* name;
    comms::Pending pending;
};

constexpr Named kPrompts[] = {{"set", comms::Pending::Set},
                              {"dfu", comms::Pending::Dfu},
                              {"recovery", comms::Pending::Recovery},
                              {"power_off", comms::Pending::PowerOff},
                              {"erase_log", comms::Pending::EraseLog},
                              {"gnss_cold_start", comms::Pending::GnssColdStart},
                              {"register", comms::Pending::Register},
                              {"capture", comms::Pending::Capture}};

comms::Pending pending_named(const char* name) {
    for (const Named& named : kPrompts)
        if (std::strcmp(named.name, name) == 0) return named.pending;
    return comms::Pending::None;
}

constexpr int kFrameBytes = 512;

bool applied(go::Settings& settings, const char* json) {
    uint8_t frame[kFrameBytes];
    const int bytes = link_text::frame_from_text(json, frame, kFrameBytes);
    if (bytes <= 0) return false;
    const cbor::Reader reader(ConstByteSpan(frame, static_cast<size_t>(bytes)));
    return go::apply_settings(settings, reader) == Status::Ok;
}

bool settings_detail(const char* set, const char* settings, char* out, int capacity) {
    go::Settings from = go::defaults();
    if (!applied(from, settings)) return false;
    go::Settings to = from;
    if (!applied(to, set)) return false;
    return go::describe_settings_changes(from, to, out, capacity) > 0;
}

// INFO: fc 14nov26 a screen reader hears the page's words in reading order, the key last
void say(char* out, int capacity, const char* title, const char* detail, uint32_t seconds_left) {
    char text[kPromptWordsBytes];
    int written = format_string(text, title);
    written += format_string(text + written, ". ");
    for (const char* at = detail; *at != 0 && written < kPromptWordsBytes - 64; at++) {
        if (*at == '\n')
            written += format_string(text + written, ", ");
        else
            text[written++] = *at;
    }
    if (written > 0 && text[written - 1] != '.') text[written++] = '.';
    text[written++] = ' ';
    written += format_string(text + written, go::kConfirmCancelLine);
    text[written++] = ' ';
    if (seconds_left == 0) {
        written += format_string(text + written, go::kConfirmOnceLine);
    } else {
        written += format_string(text + written, go::kConfirmWaitLine);
        text[written++] = ' ';
        written += format_unsigned(text + written, seconds_left);
        written += format_string(text + written, " S.");
    }
    written += format_string(text + written, " PUSH TWICE TO ");
    written += format_string(text + written, go::kConfirmAllowVerb);
    text[written++] = '.';
    text[written] = 0;
    std::strncpy(out, text, static_cast<size_t>(capacity) - 1);
    out[capacity - 1] = 0;
}

}  // namespace

bool draw_prompt(go::Glass& glass, const char* prompt, const char* argument, const char* settings,
                 uint32_t seconds_left, char* words, int words_capacity) {
    const comms::Pending pending = pending_named(prompt);
    if (pending == comms::Pending::None) return false;
    char detail[comms::kPromptDetailBytes]{};
    if (pending == comms::Pending::Set) {
        if (!settings_detail(argument, settings, detail, sizeof(detail))) return false;
    } else if (pending == comms::Pending::Dfu) {
        ports::ImageVersion version;
        if (!dfu::parse_version(argument, version)) return false;
        (void)comms::format_dfu_detail(detail, sizeof(detail), version);
    } else {
        std::strncpy(detail, comms::pending_detail(pending), sizeof(detail) - 1);
    }
    go::ConfirmSnapshot snapshot;
    snapshot.title = comms::pending_title(pending);
    snapshot.detail = detail;
    snapshot.seconds_left = seconds_left;
    go::draw_confirm(glass, snapshot);
    if (words != nullptr && words_capacity > 0)
        say(words, words_capacity, snapshot.title, detail, seconds_left);
    return true;
}

}  // namespace skyblip::simulator
