#include "products/skyblip_go/pages/bravo_log.h"

#include "core/util/format.h"
#include "products/skyblip_go/pages/frame.h"
#include "products/skyblip_go/pages/page.h"

namespace skyblip::go {

namespace {
constexpr int kLeft = 4;
using ui::kCellWidth;
constexpr int column_x(int cell) { return kLeft + cell * kCellWidth; }

constexpr int kTitleY = 2;
constexpr int kFirmwareY = 12;
constexpr int kCountsY = 22;
constexpr int kRuleY = 32;
constexpr int kFirstRowY = 36;
constexpr int kLineHeight = 10;

constexpr int kClockX = column_x(0);
constexpr int kVerdictX = column_x(13);
constexpr int kAddressX = column_x(19);
constexpr int kRightEnd = column_x(32);

void right_aligned(ui::Canvas& canvas, int y, const char* text, int length) {
    canvas.draw_text(kRightEnd - length * kCellWidth, y, text, true, 1);
}

const char* state_word(comms::BravoState state) {
    switch (state) {
        case comms::BravoState::Live: return "LIVE";
        case comms::BravoState::Waiting: return "WAIT";
        case comms::BravoState::LinkDown: return "LINK DOWN";
        case comms::BravoState::ReceiverDown: return "RECEIVER DOWN";
        case comms::BravoState::NoUtc: return "NO UTC";
        case comms::BravoState::Unpaired: break;
    }
    return "NOT PAIRED";
}

const char* verdict_word(BravoVerdict verdict) {
    switch (verdict) {
        case BravoVerdict::Refused: return "BAD";
        case BravoVerdict::Unplaced: return "TIME";
        case BravoVerdict::Folded: break;
    }
    return "ADS-B";
}

int format_stamp(char* out, const diagnostics::Instant& at) {
    if (!at.utc_dated) {
        const int written = format_string(out, "T+");
        return written + format_unsigned(out + written, at.at_s % kUptimeClockWrapS);
    }
    return format_seconds_of_day(out, at.at_s);
}

void draw_title(ui::Canvas& canvas, const BravoLogSnapshot& snapshot) {
    char text[16];
    int written = format_string(text, "BRAVO ");
    written += format_hex(text + written, snapshot.tag, 4);
    text[written] = 0;
    canvas.draw_text(kLeft, kTitleY, text, true, 1);
    const char* state = state_word(snapshot.state);
    right_aligned(canvas, kTitleY, state, text_cells(state));
}

int format_cell(char* out, const comms::PeerHealth& health) {
    int written = format_string(out, "BAT ");
    if (health.charging) return written + format_string(out + written, "CHG");
    if (!health.battery_percent) return written + format_string(out + written, "--");
    written += format_unsigned(out + written, *health.battery_percent);
    return written + format_string(out + written, "%");
}

void draw_health(ui::Canvas& canvas, const comms::PeerHealth& health) {
    char text[24];
    int written = format_string(text, "FW ");
    written += format_string(text + written, health.firmware[0] != 0 ? health.firmware : "--");
    text[written] = 0;
    canvas.draw_text(kLeft, kFirmwareY, text, true, 1);
    written = format_cell(text, health);
    text[written] = 0;
    right_aligned(canvas, kFirmwareY, text, written);

    written = format_string(text, "1090 ");
    if (health.frame_rate_hz) {
        written += format_unsigned(text + written, *health.frame_rate_hz);
        written += format_string(text + written, "/S");
    } else {
        written += format_string(text + written, "OFF");
    }
    text[written] = 0;
    canvas.draw_text(kLeft, kCountsY, text, true, 1);
}

void draw_counts(ui::Canvas& canvas, const BravoLogSnapshot& snapshot) {
    char text[32];
    int written = format_string(text, "B ");
    written += format_unsigned(text + written, static_cast<uint32_t>(snapshot.tracked));
    written += format_string(text + written, " IN ");
    written += format_unsigned(text + written, snapshot.folded);
    if (snapshot.refused + snapshot.unplaced > 0) {
        written += format_string(text + written, " OUT ");
        written += format_unsigned(text + written, snapshot.refused + snapshot.unplaced);
    }
    text[written] = 0;
    right_aligned(canvas, kCountsY, text, written);
}

void draw_row(ui::Canvas& canvas, int y, const BravoLogEntry& entry) {
    char text[16];
    int written = format_stamp(text, entry.at);
    text[written] = 0;
    canvas.draw_text(kClockX, y, text, true, 1);
    canvas.draw_text(kVerdictX, y, verdict_word(entry.verdict), true, 1);
    if (!entry.address_valid) return;
    written = format_hex(text, entry.address, 6);
    text[written] = 0;
    canvas.draw_text(kAddressX, y, text, true, 1);

    if (entry.callsign[0] != 0) {
        right_aligned(canvas, y, entry.callsign, text_cells(entry.callsign));
        return;
    }
    if (!entry.pressure_altitude_valid) return;
    written = format_string(text, "FL");
    written += format_signed(text + written, entry.pressure_altitude_ft / 100, 3, 0, false);
    text[written] = 0;
    right_aligned(canvas, y, text, written);
}

}  // namespace

void draw_bravo_log(ui::Canvas& canvas, const BravoLogSnapshot& snapshot) {
    draw_title(canvas, snapshot);
    if (snapshot.health != nullptr) draw_health(canvas, *snapshot.health);
    if (snapshot.state != comms::BravoState::Unpaired) draw_counts(canvas, snapshot);
    canvas.horizontal_line(kLeft, kRuleY, kRightEnd - kLeft, true);

    if (snapshot.log == nullptr || snapshot.log->count() == 0) {
        canvas.draw_text(kLeft, kFirstRowY + kLineHeight, "NO FRAME FROM BRAVO", true, 1);
        return;
    }
    const int rows = snapshot.log->count();
    for (int i = 0; i < rows; i++)
        draw_row(canvas, kFirstRowY + i * kLineHeight, snapshot.log->newest(i));
}

}  // namespace skyblip::go
