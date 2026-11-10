// A target that sends no altitude is never drawn, listed or sent to the tablet as level with us.
#include <string>
#include <vector>

#include "core/events/rf.h"
#include "core/protocol/adsl.h"
#include "core/protocol/air.h"
#include "core/units/units.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "products/skyblip_go/pages/nearby.h"
#include "test/support/glass_text.h"
#include "test/support/nmea_stream.h"
#include "test/support/product_rig.h"
#include "ui/widgets/blip.h"

using namespace skyblip;

namespace {

constexpr uint32_t kAddress = 0xC5D804;

void hear_without_altitude(Rig& rig, int32_t north_m) {
    model::Ownship transmitter = rig.state().own;
    transmitter.latitude_1e7 +=
        static_cast<int32_t>(static_cast<int64_t>(north_m) * 1000000 / 11132);
    transmitter.speed_mm_s = 0;
    transmitter.vdop_e2 = 150;

    protocol::AdslPacket packet;
    protocol::from_own(packet, transmitter, kAddress, /*address_mapping_table=*/6,
                       /*aircraft_category=*/4);
    packet.set_altitude_invalid();
    packet.scramble();
    packet.set_crc();

    uint8_t chips[protocol::kTxChipBytes];
    const size_t chip_bytes = protocol::encode_m_band(protocol::kAdslSyncWord, packet.data,
                                                      protocol::kAdslFrameBytes, chips);
    events::RfEvent event{};
    event.type = events::RfEventType::RxDone;
    event.rssi_dbm = -80;
    event.bytes = models::Sx1262::deliver_after_sync(
        chips, static_cast<uint8_t>(chip_bytes), protocol::kSharedSync, protocol::kSharedSyncBits,
        event.data.data(), protocol::kRxChipBytes);
    rig.product.bus().rf.push(event);
}

void hear_for(Rig& rig, uint32_t& now_ms, int32_t north_m, int seconds) {
    for (int second = 0; second < seconds; second++) {
        hear_without_altitude(rig, north_m);
        fly(rig, now_ms, 1);
    }
}

bool blip_anywhere(const ui::Canvas& glass, ui::Vertical vertical) {
    constexpr int kHalf = 8;
    ui::Panel<2 * kHalf + 1, 2 * kHalf + 1> alone;
    alone.clear(true);
    ui::draw_blip(alone, kHalf, kHalf, vertical, ui::Trend::Steady, false);
    for (int y = kHalf; y < go::kGlassHeight - kHalf; y++)
        for (int x = kHalf; x < go::kGlassWidth - kHalf; x++) {
            bool same = true;
            for (int dy = -kHalf; dy <= kHalf && same; dy++)
                for (int dx = -kHalf; dx <= kHalf && same; dx++)
                    same =
                        glass.get_pixel(x + dx, y + dy) == alone.get_pixel(kHalf + dx, kHalf + dy);
            if (same) return true;
        }
    return false;
}

}  // namespace

TEST_CASE("unknown altitude: an advisory on distance, a PFLAU with no vertical, and no PFLAA") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    fly(rig, now_ms, 3);
    rig.raise_link();
    fly(rig, now_ms, 1);

    hear_for(rig, now_ms, 1500, 4);
    REQUIRE(rig.state().traffic.count() == 1);
    CHECK(rig.state().alarm_level == traffic::Level::Advisory);

    std::string status;
    for (const std::string& sentence : sentences(rig))
        if (sentence.rfind("$PFLAU,", 0) == 0) status = sentence;
    const std::vector<std::string> values = fields(status);
    REQUIRE(values.size() >= 11);
    CHECK(values[5] == "1");
    CHECK(values[8].empty());
    CHECK(std::stoi(values[9]) > 1300);
    CHECK(values[10] == "C5D804");
    CHECK(count_of(rig, "$PFLAA,") == 0);
}

TEST_CASE("unknown altitude: a ring on the radar and two dashes on nearby") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    fly(rig, now_ms, 3);

    hear_for(rig, now_ms, 5000, 3);
    REQUIRE(rig.state().traffic.count() == 1);
    REQUIRE(rig.state().alarm_level == traffic::Level::None);
    REQUIRE(rig.product.screen().page() == go::Page::Radar);
    CHECK(blip_anywhere(rig.product.screen().framebuffer(), ui::Vertical::Unknown));
    CHECK_FALSE(blip_anywhere(rig.product.screen().framebuffer(), ui::Vertical::Level));

    rig.show(now_ms, go::Page::Nearby);
    hear_for(rig, now_ms, 5000, 2);
    REQUIRE(rig.product.screen().page() == go::Page::Nearby);
    const int dashes_x = go::kNearbyRelativeAltitudeEnd - 2 * ui::kCellWidth * go::kNearbyScale;
    CHECK(reads_at(rig.product.screen().framebuffer(), dashes_x, go::kNearbyFirstRowY, "--",
                   go::kNearbyScale));
}
