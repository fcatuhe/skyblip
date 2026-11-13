// What the 1090 MHz receiver counts for the bench, and how deep its queue is and why.
#include "core/bus/bus.h"
#include "core/events/adsb.h"
#include "doctest/doctest.h"
#include "ports/adsb_receiver.h"
#include "protocols/adsb/assembly.h"

using namespace skyblip;
using ports::AdsbReceiverStatistics;

namespace {

constexpr uint64_t kSecondUs = 1000000;

}  // namespace

TEST_CASE("adsb receiver: each RSSI bucket starts at its floor and the last one is open") {
    CHECK(AdsbReceiverStatistics::bucket_of(-128) == 0);
    CHECK(AdsbReceiverStatistics::bucket_of(-91) == 0);
    CHECK(AdsbReceiverStatistics::bucket_of(-90) == 1);
    CHECK(AdsbReceiverStatistics::bucket_of(-81) == 1);
    CHECK(AdsbReceiverStatistics::bucket_of(-80) == 2);
    CHECK(AdsbReceiverStatistics::bucket_of(-31) == 6);
    CHECK(AdsbReceiverStatistics::bucket_of(-30) == 7);
    CHECK(AdsbReceiverStatistics::bucket_of(10) == 7);
}

// Two frames read in one wake share the packet status, so only the newest has a level.
TEST_CASE("adsb receiver: every frame is counted, only a measured level lands in the histogram") {
    AdsbReceiverStatistics statistics;
    statistics.heard(-75, true, 0);
    statistics.heard(0, false, 0);
    CHECK(statistics.frames == 2);
    uint32_t histogram = 0;
    for (const uint32_t count : statistics.rssi) histogram += count;
    CHECK(histogram == 1);
    CHECK(statistics.rssi[2] == 1);
}

TEST_CASE("adsb receiver: the frame rate is the last whole second, and a silent one reads zero") {
    AdsbReceiverStatistics statistics;
    for (int i = 0; i < 3; i++) statistics.heard(-70, true, 5 * kSecondUs + 1000 * i);
    statistics.roll(5 * kSecondUs + 900000);
    CHECK(statistics.frame_rate_hz == 0);
    statistics.roll(6 * kSecondUs + 10);
    CHECK(statistics.frame_rate_hz == 3);
    statistics.roll(8 * kSecondUs);
    CHECK(statistics.frame_rate_hz == 0);
    CHECK(statistics.frames == 3);
}

// 100 aircraft at DO-260B's average of 6.2 squitters a second, over 100 ms, is 62 frames.
TEST_CASE("adsb receiver: the queue holds the average load over the drain window") {
    CHECK(events::kAdsbDesignAircraft == adsb::Assembly::kMaxAircraft);
    CHECK(events::kAdsbQueueFrames == 62);
    bus::AdsbQueue queue;
    events::AdsbFrame frame{};
    for (int i = 0; i < events::kAdsbQueueFrames; i++) CHECK(queue.push(frame));
    CHECK_FALSE(queue.push(frame));
    CHECK(queue.dropped() == 1);
}

TEST_CASE("adsb receiver: a frame crosses the queue in tens of bytes, not RfEvent's hundreds") {
    CHECK(sizeof(events::AdsbFrame) <= 40);
    CHECK(sizeof(events::AdsbFrame) * 4 < sizeof(events::RfEvent));
}
