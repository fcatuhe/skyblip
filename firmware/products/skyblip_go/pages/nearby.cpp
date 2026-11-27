#include "products/skyblip_go/pages/nearby.h"

#include "core/model/aircraft.h"
#include "core/units/units.h"
#include "core/util/format.h"
#include "products/skyblip_go/pages/bravo_word.h"
#include "products/skyblip_go/pages/frame.h"
#include "products/skyblip_go/pages/page.h"
#include "protocols/common/geometry.h"

namespace skyblip::go {

namespace {
using ui::kCellWidth;
using ui::kCharacterHeight;
constexpr int kRuleDrop = 10;
constexpr int kFigureUnitGap = 1;
constexpr int kRelativeHundredsCeiling = 99;
constexpr int32_t kDistanceCeilingE1 = 999;

void figure(ui::Canvas& canvas, int x_end, int y, const char* value, const char* unit) {
    int x = x_end;
    if (unit[0] != 0) {
        x -= text_cells(unit) * kCellWidth;
        canvas.draw_text(x, y + kCharacterHeight * (kNearbyScale - 1), unit, true, 1);
        x -= kFigureUnitGap;
    }
    canvas.draw_text(x - text_cells(value) * kNearbyCellWidth, y, value, true, kNearbyScale);
}

void battery(ui::Canvas& canvas, int y, std::optional<uint8_t> percent, bool charging) {
    if (charging) {
        right_aligned(canvas, kNearbyBatteryEnd, y, "CHG", kNearbyScale);
        return;
    }
    if (!percent) {
        right_aligned(canvas, kNearbyBatteryEnd, y, "--", kNearbyScale);
        return;
    }
    char text[4];
    text[format_unsigned(text, *percent)] = 0;
    figure(canvas, kNearbyBatteryEnd, y, text, "%");
}

void satellites_and_traffic(ui::Canvas& canvas, int y, std::optional<uint8_t> satellites,
                            int traffic) {
    char text[8];
    if (satellites)
        text[format_unsigned(text, *satellites)] = 0;
    else
        text[format_string(text, "--")] = 0;
    figure(canvas, kNearbySatellitesEnd, y, text, "SAT");
    text[format_unsigned(text, static_cast<uint32_t>(traffic))] = 0;
    figure(canvas, kNearbyRelativeAltitudeEnd, y, text, "TFC");
}

void unit_row(ui::Canvas& canvas, int y, const char* label, std::optional<uint8_t> percent,
              bool charging, const char* word, std::optional<uint8_t> satellites, int traffic) {
    canvas.draw_text(kNearbyIdX, y, label, true, kNearbyScale);
    battery(canvas, y, percent, charging);
    if (word != nullptr)
        right_aligned(canvas, kNearbyRelativeAltitudeEnd, y, word, kNearbyScale);
    else
        satellites_and_traffic(canvas, y, satellites, traffic);
}

const char* stage_word(gnss::Stage stage) {
    const char* word = gnss::stage_name(stage);
    return word[0] != 0 ? word : "NO FIX";
}

// INFO: fc 27nov26 what the Go transmits is NO TX's to say, so a fix always shows its traffic
const char* go_word(const NearbySnapshot& snapshot) {
    return snapshot.fix_valid && !snapshot.bravo_fix ? nullptr : stage_word(snapshot.go.stage);
}

void draw_units(ui::Canvas& canvas, const NearbySnapshot& snapshot) {
    unit_row(canvas, kNearbyUnitY, "GO", snapshot.go.battery_percent, snapshot.go.charging,
             go_word(snapshot), snapshot.go.satellites, snapshot.go.traffic);
    const NearbyBravo& bravo = snapshot.bravo;
    if (bravo.state == comms::BravoState::Unpaired) return;
    unit_row(canvas, kNearbyUnitY + kNearbyLineHeight, "B", bravo.battery_percent, bravo.charging,
             bravo_word(bravo.state), bravo.satellites, bravo.traffic);
}

void banner(ui::Canvas& canvas, int y, const char* text) {
    canvas.draw_text((kGlassWidth - text_cells(text) * kNearbyCellWidth) / 2, y, text, true,
                     kNearbyScale);
}

void draw_head(ui::Canvas& canvas, int y) {
    canvas.draw_text(kNearbyIdX, y, "SRC ID", true, 1);
    right_aligned(canvas, kNearbyDistanceEnd, y, "DIST");
    right_aligned(canvas, kNearbyRelativeAltitudeEnd, y, "ALT");
    canvas.horizontal_line(kNearbyIdX, y + kRuleDrop, kNearbyRelativeAltitudeEnd - kNearbyIdX,
                           true);
}

int32_t relative_hundreds_of_feet(int32_t up_m) {
    const int32_t feet = to_feet(Metres(up_m)).value;
    const int32_t rounded = (feet >= 0 ? feet + 50 : feet - 50) / 100;
    if (rounded > kRelativeHundredsCeiling) return kRelativeHundredsCeiling;
    if (rounded < -kRelativeHundredsCeiling) return -kRelativeHundredsCeiling;
    return rounded;
}

void draw_id(ui::Canvas& canvas, int y, const traffic::RangeRow& row) {
    char text[16];
    text[0] = model::source_letter(row.source);
    text[1] = 0;
    canvas.draw_text(kNearbyIdX, y, text, true, kNearbyScale);
    if (row.callsign == nullptr || row.callsign[0] == 0) {
        text[format_hex(text, row.address, 6)] = 0;
        canvas.draw_text(kNearbyAddressX, y + kNearbyAddressDrop, text, true, 1);
        return;
    }
    int written = 0;
    while (written < kNearbyIdCharacters - 2 && row.callsign[written] != 0) {
        text[written] = row.callsign[written];
        written++;
    }
    text[written] = 0;
    canvas.draw_text(kNearbyAddressX, y, text, true, kNearbyScale);
}

void draw_row(ui::Canvas& canvas, int y, const traffic::RangeRow& row, bool metric) {
    char text[16];
    draw_id(canvas, y, row);

    const int32_t distance_e1 =
        metric ? wire::divide_rounded(row.ground_m, 100) : to_nm_e1(Metres(row.ground_m)).value;
    int written = distance_e1 > kDistanceCeilingE1
                      ? format_string(text, "FAR")
                      : format_unsigned(text, static_cast<uint32_t>(distance_e1), 2, 1);
    text[written] = 0;
    right_aligned(canvas, kNearbyDistanceEnd, y, text, kNearbyScale);

    if (row.up_valid) {
        const int32_t relative = relative_hundreds_of_feet(row.up_m);
        written = format_signed(text, relative, 2, 0, relative != 0);
    } else {
        written = format_string(text, "--");
    }
    text[written] = 0;
    right_aligned(canvas, kNearbyRelativeAltitudeEnd, y, text, kNearbyScale);
}

}  // namespace

void draw_nearby(ui::Canvas& canvas, const NearbySnapshot& snapshot) {
    draw_units(canvas, snapshot);
    const bool paired = snapshot.bravo.state != comms::BravoState::Unpaired;
    const int shift = nearby_shift(paired);
    draw_head(canvas, kNearbyHeadY + shift);

    if (!snapshot.fix_valid) {
        banner(canvas, kNearbyBannerY + shift, "NO FIX");
        return;
    }
    if (snapshot.row_count == 0 || snapshot.rows == nullptr) {
        banner(canvas, kNearbyBannerY + shift, "NO TRAFFIC");
        return;
    }

    const int fit = kNearbyRows - (paired ? 1 : 0);
    const int rows = snapshot.row_count < fit ? snapshot.row_count : fit;
    const bool metric = snapshot.units == go::Units::Metric;
    for (int i = 0; i < rows; i++)
        draw_row(canvas, kNearbyFirstRowY + shift + i * kNearbyLineHeight, snapshot.rows[i],
                 metric);
}

}  // namespace skyblip::go
