#ifndef SKYBLIP_CORE_BUS_BUS_H
#define SKYBLIP_CORE_BUS_BUS_H

#include <atomic>
#include <cstdint>

#include "core/events/input.h"
#include "core/events/link.h"
#include "core/events/rf.h"
#include "core/events/sensor.h"
#include "core/gnss/nmea.h"

namespace skyblip::bus {

// INFO: fc 04sep26 one producer, one consumer, two threads on silicon: release/acquire is the lock
template <class T, int N>
class Queue {
   public:
    static constexpr int kCapacity = N;

    bool push(const T& item) {
        const int head = head_.load(std::memory_order_relaxed);
        const int next = (head + 1) % (N + 1);
        if (next == tail_.load(std::memory_order_acquire)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        slot_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& out) {
        const int tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        out = slot_[tail];
        tail_.store((tail + 1) % (N + 1), std::memory_order_release);
        return true;
    }

    bool empty() const {
        return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire);
    }
    uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

   private:
    T slot_[N + 1]{};
    std::atomic<int> head_{0};
    std::atomic<int> tail_{0};
    std::atomic<uint32_t> dropped_{0};
};

struct Bus {
    Queue<gnss::GnssSolution, 2> gnss;
    Queue<events::BarometerSample, 2> barometer;
    Queue<events::AccelerometerSample, 2> accelerometer;
    Queue<events::RfEvent, 8> rf;
    // The connection itself, ahead of the bytes that travel over it: the config
    // service is the single reader, because it is the one that holds what a link
    // being up or gone means (a standing prompt, an upload window, the gauge it
    // pushes unsolicited).
    Queue<events::LinkEvent, 4> link_events;
    Queue<events::RxFrame, 4> link_rx;
    // One writer, one reader, per §5.3: the config service drains link_rx with a
    // while-pop, so a log command sharing that queue would be read and dropped
    // by the wrong service.
    Queue<events::RxFrame, 2> log_rx;
    Queue<events::ContactEvent, 8> input;
    Queue<events::BatterySample, 2> battery;
};

}  // namespace skyblip::bus

#endif
