#include "products/skyblip_go/pages/self_test.h"

#include "core/util/format.h"
#include "products/skyblip_go/pages/page.h"

namespace skyblip::go {

namespace {

constexpr int kLeft = kSelfTestLeftX;
constexpr int kRight = kSelfTestRightX;
using ui::kCellWidth;
constexpr int kLineHeight = kSelfTestRowHeight;

int length(const char* text) {
    int count = 0;
    while (text[count]) count++;
    return count;
}

void right_aligned(ui::Canvas& canvas, int x_end, int y, const char* text) {
    canvas.draw_text(x_end - length(text) * kCellWidth, y, text, true, 1);
}

const char* verdict(PartState state) {
    switch (state) {
        case PartState::Pass: return "PASS";
        case PartState::Fail: return "FAIL";
        case PartState::Absent: break;
    }
    return "NOT FITTED";
}

// A leader of dots between the name and its verdict, so an eye running down the
// right-hand column does not lose which row it is on.
void leaders(ui::Canvas& canvas, int from_x, int to_x, int y) {
    for (int x = from_x; x < to_x; x += 2 * kCellWidth) canvas.draw_text(x, y, ".", true, 1);
}

const char* bravo_verdict(comms::BravoState state) {
    switch (state) {
        case comms::BravoState::Live: return "LIVE";
        case comms::BravoState::Waiting: return "WAITING";
        case comms::BravoState::LinkDown: return "LINK DOWN";
        case comms::BravoState::ReceiverDown: return "RECEIVER DOWN";
        case comms::BravoState::NoUtc: return "NO UTC";
        case comms::BravoState::Unpaired: break;
    }
    return "";
}

void bravo_row(ui::Canvas& canvas, int y, const SelfTestSnapshot& snapshot) {
    char name[12];
    int written = format_string(name, "BRAVO ");
    written += format_hex(name + written, snapshot.bravo_tag, 4);
    name[written] = 0;
    const char* mark = bravo_verdict(snapshot.bravo);
    canvas.draw_text(kLeft, y, name, true, 1);
    leaders(canvas, kLeft + (written + 1) * kCellWidth, kRight - (length(mark) + 1) * kCellWidth,
            y);
    right_aligned(canvas, kRight, y, mark);
}

}  // namespace

void draw_self_test(ui::Canvas& canvas, const SelfTestSnapshot& snapshot) {
    canvas.clear(true);

    char text[32];
    int written = format_string(text, "SELF TEST");
    text[written] = 0;
    canvas.draw_text(kLeft, 3, text, true, 2);
    canvas.horizontal_line(kLeft, 21, kGlassWidth - 2 * kLeft, true);

    // Identity and why the device is running at all, on one line: the pair a
    // bench log needs before anything below it means something.
    written = format_string(text, "ID ");
    written += format_air_address(text + written, snapshot.address);
    text[written] = 0;
    canvas.draw_text(kLeft, kSelfTestHeaderY, text, true, 1);
    right_aligned(canvas, kRight, kSelfTestHeaderY, snapshot.reset_reason);

    int y = kSelfTestFirstRowY;
    const int rows = snapshot.part_count < kSelfTestRows ? snapshot.part_count : kSelfTestRows;
    for (int i = 0; i < rows; i++) {
        const SelfTestPart& part = snapshot.parts[i];
        const char* mark = verdict(part.state);
        int name_end = kLeft + (length(part.name) + 1) * kCellWidth;
        canvas.draw_text(kLeft, y, part.name, true, 1);
        // The identity sits with the name, not with the verdict: a row reads
        // "what it is" then "how it answered", left to right.
        if (part.detail) {
            canvas.draw_text(name_end, y, part.detail, true, 1);
            name_end += (length(part.detail) + 1) * kCellWidth;
        }
        leaders(canvas, name_end, kRight - (length(mark) + 1) * kCellWidth, y);
        right_aligned(canvas, kRight, y, mark);
        y += kLineHeight;
    }

    if (snapshot.i2c_addresses && snapshot.i2c_address_count > 0 && rows < kSelfTestRows) {
        char bus[kSelfTestRowCells + 1];
        written = format_string(bus, "I2C");
        for (int i = 0; i < snapshot.i2c_address_count; i++) {
            const char* role = snapshot.i2c_roles ? snapshot.i2c_roles[i] : nullptr;
            if (written + 1 + (role ? length(role) : 2) > kSelfTestRowCells) break;
            written += format_string(bus + written, " ");
            written += role ? format_string(bus + written, role)
                            : format_hex(bus + written, snapshot.i2c_addresses[i], 2);
        }
        bus[written] = 0;
        canvas.draw_text(kLeft, y, bus, true, 1);
        y += kLineHeight;
    }

    // INFO: fc 12nov26 one row for both: a loss or a wait outranks the own-ICAO warning, then LIVE
    const bool paired = snapshot.bravo != comms::BravoState::Unpaired;
    const bool warned = snapshot.own_icao_warning && !comms::lost(snapshot.bravo) &&
                        snapshot.bravo != comms::BravoState::Waiting;
    if ((paired || warned) && y < self_test_row_y(kSelfTestRows)) {
        if (warned)
            canvas.draw_text(kLeft, y, kSelfTestOwnIcaoText, true, 1);
        else
            bravo_row(canvas, y, snapshot);
        y += kLineHeight;
    }

    y += kSelfTestDividerGap;
    canvas.horizontal_line(kLeft, y, kGlassWidth - 2 * kLeft, true);
    y += kSelfTestFooterGap;

    written = 0;
    if (snapshot.battery_valid) {
        // Centivolts: at boot the number that matters is whether the pack can
        // carry a flight, and two decimals is what a cell is judged on.
        written = format_string(text, "BAT ");
        written += format_unsigned(text + written, (snapshot.battery_mv + 5u) / 10u, 1, 2);
        written += format_string(text + written, " V");
    }
    if (snapshot.went_dark_flat)
        written += format_string(text + written, written > 0 ? " WAS FLAT" : "WAS FLAT");
    text[written] = 0;
    canvas.draw_text(kLeft, y, text, true, 1);

    // The verdict, inverted so it cannot be read as one more row.
    const char* status = snapshot.flyable ? "READY" : "GROUNDED";
    const int width = (length(status) + 2) * kCellWidth;
    canvas.rectangle(kRight - width, y - kSelfTestVerdictRise, width, kSelfTestVerdictHeight, true,
                     /*fill=*/true);
    canvas.draw_text(kRight - width + kCellWidth, y, status, false, 1);
}

}  // namespace skyblip::go
