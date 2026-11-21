#ifndef SKYBLIP_CORE_BUS_BUS_H
#define SKYBLIP_CORE_BUS_BUS_H

#include <atomic>
#include <cstdint>

#include "core/events/adsb.h"
#include "core/events/central.h"
#include "core/events/input.h"
#include "core/events/link.h"
#include "core/events/rf.h"
#include "core/events/sensor.h"
#include "core/gnss/nmea.h"

namespace skyblip::bus {

// INFO: fc 04sep26 one producer, one consumer, two threads on silicon: release/acquire is the lock
template <class Item, int Capacity>
class Queue {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "the index is a counter masked by a power of two");
    static_assert(Capacity <= 0x8000, "a full queue's fill must fit the 16-bit counters");
    static_assert(std::atomic<uint16_t>::is_always_lock_free);

   public:
    bool push(const Item& item) {
        const uint16_t head = head_.load(std::memory_order_relaxed);
        if (static_cast<uint16_t>(head - tail_.load(std::memory_order_acquire)) == Capacity) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        slots_[head & (Capacity - 1)] = item;
        head_.store(static_cast<uint16_t>(head + 1), std::memory_order_release);
        return true;
    }

    bool pop(Item& out) {
        const uint16_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        out = slots_[tail & (Capacity - 1)];
        tail_.store(static_cast<uint16_t>(tail + 1), std::memory_order_release);
        return true;
    }

    bool empty() const {
        return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire);
    }
    uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

   private:
    Item slots_[Capacity]{};
    std::atomic<uint16_t> head_{0};
    std::atomic<uint16_t> tail_{0};
    std::atomic<uint32_t> dropped_{0};
};

// INFO: fc 11nov26 only Bravo hears 1090 MHz, so the queue is a type and its product holds it
using AdsbQueue = Queue<events::AdsbFrame, events::kAdsbQueueFrames>;
using RfQueue = Queue<events::RfEvent, 8>;
using CentralQueue = Queue<events::CentralEvent, 8>;
using AccelerometerQueue = Queue<events::AccelerometerSample, 2>;

struct Bus {
    Queue<gnss::GnssSolution, 2> gnss;
    Queue<events::BarometerSample, 2> barometer;
    Queue<events::LinkEvent, 4> link_events;
    Queue<events::RxFrame, 4> link_rx;
    // INFO: fc 20nov26 config drains link_rx with a while-pop, so log frames need their own queue
    Queue<events::RxFrame, 2> log_rx;
    // INFO: fc 11nov26 the peer endpoint: the Go's BravoService drains it, and Bravo's PeerService
    Queue<events::RxFrame, 4> peer_rx;
    Queue<events::ContactEvent, 8> input;
    Queue<events::BatterySample, 2> battery;

    bool deliver(const events::RxFrame& frame) {
        switch (frame.endpoint) {
            case events::Endpoint::Log: return log_rx.push(frame);
            case events::Endpoint::Peer: return peer_rx.push(frame);
            default: return link_rx.push(frame);
        }
    }
};

// INFO: fc 19nov26 the Go's SX1262, central and IMU: Bravo has none of them, so it holds no slot
struct GoBus : Bus {
    RfQueue rf;
    CentralQueue central_events;
    AccelerometerQueue accelerometer;
};

}  // namespace skyblip::bus

#endif
