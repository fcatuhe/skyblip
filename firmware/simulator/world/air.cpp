#include "simulator/world/air.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/model/aircraft.h"

namespace skyblip::simulator {

namespace {
const char* kEventName[] = {"TX  ", "RX  ", "DEAF", "COLL"};

// The radio reports the frequency its PLL word resolves to, a few hertz off the
// channel it was asked for.
const char* channel_name(uint32_t frequency_hz) {
    if (Air::tuned_to(frequency_hz, timing::kOBandHz)) return "869.525";
    if (Air::tuned_to(frequency_hz, timing::kMBand1Hz)) return "868.400";
    return "868.200";
}
}  // namespace

void Air::emit(uint64_t at_us, uint32_t frequency_hz, const uint8_t* chips, uint16_t bytes,
               int8_t rssi_dbm, uint32_t chip_rate_bps) {
    for (Burst& burst : bursts_) {
        if (burst.used) continue;
        burst = Burst{};
        burst.used = true;
        burst.at_us = at_us;
        burst.frequency_hz = frequency_hz;
        burst.rssi_dbm = rssi_dbm;
        burst.chip_rate_bps = chip_rate_bps;
        burst.bytes =
            bytes > sizeof(burst.chips) ? static_cast<uint16_t>(sizeof(burst.chips)) : bytes;
        burst.air_time_us = air_time_us(burst.bytes, chip_rate_bps);
        std::memcpy(burst.chips, chips, burst.bytes);
        return;
    }
}

bool Air::detected_by(models::Sx1262& radio, Burst& burst) {
    if (burst.collided) {
        collisions_++;
        log(burst, AirEvent::Collision);
        radio.receive_air(burst.chips, burst.bytes, /*crc_error=*/true, burst.rssi_dbm,
                          burst.chip_rate_bps);
        return true;
    }
    if (!radio.receive_air(burst.chips, burst.bytes, /*crc_error=*/false, burst.rssi_dbm,
                           burst.chip_rate_bps))
        return false;
    heard_++;
    log(burst, AirEvent::Rx);
    return true;
}

void Air::step(uint64_t now_us, models::Sx1262& radio) {
    if (tx_in_flight_ && now_us >= tx_done_at_us_) {
        tx_in_flight_ = false;
        radio.signal_tx_done();
    }

    for (Burst& burst : bursts_) {
        if (!burst.used || burst.started || now_us < burst.at_us) continue;
        burst.started = true;
        // A receiver has to be tuned to the channel when the preamble arrives,
        // not when the frame ends.
        burst.heard = !burst.from_ownship && radio.receiving &&
                      tuned_to(radio.frequency_hz, burst.frequency_hz) &&
                      burst.rssi_dbm > kSensitivityDbm;
        for (Burst& other : bursts_) {
            if (&other == &burst || !other.used || !other.started) continue;
            if (!tuned_to(other.frequency_hz, burst.frequency_hz)) continue;
            if (burst.at_us < other.at_us + other.air_time_us) {
                burst.collided = true;
                other.collided = true;
            }
        }
    }

    for (Burst& burst : bursts_) {
        if (!burst.used || !burst.started || now_us < burst.at_us + burst.air_time_us) continue;
        if (burst.from_ownship) {
            log(burst, AirEvent::Tx);
        } else if (!burst.heard || !detected_by(radio, burst)) {
            deaf_++;
            log(burst, AirEvent::Deaf);
        }
        burst.used = false;
    }

    set_carrier(now_us, radio);
}

// Own-ship's burst is air like any other: it occupies the channel, it collides,
// and the radio only reports TxDone once it has actually been sent.
void Air::take_own_transmission(uint64_t now_us, models::Sx1262& radio) {
    uint8_t chips[protocol::kTxChipBytes] = {0};
    uint8_t bytes = 0;
    if (!radio.take_tx(chips, bytes)) return;
    if (bytes > sizeof(chips)) bytes = static_cast<uint8_t>(sizeof(chips));
    emit(now_us, radio.frequency_hz, chips, bytes, 0);
    for (Burst& burst : bursts_)
        if (burst.used && !burst.started && burst.at_us == now_us &&
            burst.frequency_hz == radio.frequency_hz)
            burst.from_ownship = true;
    tx_in_flight_ = true;
    tx_done_at_us_ = now_us + kAirTimeUs;
}

void Air::set_carrier(uint64_t now_us, models::Sx1262& radio) {
    int8_t level = kNoiseFloorDbm;
    for (const Burst& burst : bursts_) {
        if (!burst.used || burst.from_ownship) continue;
        if (now_us < burst.at_us || now_us >= burst.at_us + burst.air_time_us) continue;
        if (!tuned_to(radio.frequency_hz, burst.frequency_hz)) continue;
        level = std::max(burst.rssi_dbm, level);
    }
    radio.rssi_dbm = level;
}

void Air::log(const Burst& burst, AirEvent event) {
    AirRecord& record = records_[count_ % kMaxRecords];
    record.at_us = burst.at_us;
    record.frequency_hz = burst.frequency_hz;
    // The phase belongs to the burst's own instant. Taking it at log time, which
    // is when the burst ENDS, dates a burst at 995 ms into the next second.
    record.phase_ms = static_cast<uint16_t>(burst.at_us / 1000 % 1000);
    record.event = event;
    record.rssi_dbm = burst.rssi_dbm;
    record.bytes = burst.bytes;
    record.chip_rate_bps = burst.chip_rate_bps;
    std::memcpy(record.chips, burst.chips, burst.bytes);
    count_++;
}

bool Air::framed(const AirRecord& record, protocol::Frame& out) {
    uint8_t payload[protocol::kRxChipBytes] = {0};
    models::Sx1262::deliver_after_sync(record.chips, record.bytes, protocol::kSharedSync,
                                       protocol::kSharedSyncBits, payload, sizeof(payload));
    return protocol::receive_m_band(payload, sizeof(payload), out);
}

bool Air::framed_uplink(const AirRecord& record, uint8_t* frame) {
    return models::Sx1262::deliver_after_sync(record.chips, record.bytes, protocol::kUplinkSync,
                                              protocol::kUplinkSyncBits, frame,
                                              protocol::kUplinkFrameBytes) != 0;
}

const AirRecord& Air::record_at(int index) const {
    const int first = count_ <= kMaxRecords ? 0 : count_ % kMaxRecords;
    return records_[(first + (index < 0 ? 0 : index)) % kMaxRecords];
}

void Air::clear() {
    for (Burst& burst : bursts_) burst.used = false;
    count_ = 0;
    heard_ = deaf_ = collisions_ = 0;
    tx_in_flight_ = false;
}

// One line per burst, framed and decoded the way a receiver on the shared sync
// window would: name the system, then read what that system puts in the clear.
// ALP-TAS codes position against the receiver's own, which a channel log does
// not have, so it shows the address the frame carries unencrypted.
int Air::format(int index, char* out, int capacity) const {
    if (index < 0 || index >= record_count()) {
        if (capacity > 0) out[0] = 0;
        return 0;
    }
    const AirRecord& record = record_at(index);
    const int head =
        std::snprintf(out, static_cast<size_t>(capacity), "%4u ms %s %s %3d dBm %3u B ",
                      record.phase_ms, channel_name(record.frequency_hz),
                      kEventName[static_cast<int>(record.event)], record.rssi_dbm, record.bytes);

    const size_t left = static_cast<size_t>(capacity - head);

    // The O band carries one thing and it is not Manchester-coded, so it is read
    // its own way: how many aircraft the ground station put in the frame, which
    // is the number a relay is worth judging by.
    if (tuned_to(record.frequency_hz, timing::kOBandHz)) {
        uint8_t frame_bytes[protocol::kUplinkFrameBytes] = {0};
        if (!framed_uplink(record, frame_bytes))
            return head + std::snprintf(out + head, left, "unframed");
        protocol::AdslUplink codec;
        model::AircraftObservation relayed[protocol::AdslUplink::kMaxTargets];
        protocol::AdslUplink::DecodeStatistics statistics{};
        if (codec.decode(frame_bytes, relayed, protocol::AdslUplink::kMaxTargets, statistics) !=
            Status::Ok)
            return head + std::snprintf(out + head, left, "UPLINK RS BAD");
        return head + std::snprintf(out + head, left, "UPLINK %d aircraft, %d corrected",
                                    statistics.targets, statistics.corrected);
    }

    protocol::Frame frame{};
    if (!framed(record, frame)) return head + std::snprintf(out + head, left, "unframed");

    if (frame.system == protocol::System::Alptas) {
        const uint32_t address = protocol::alptas_address(frame.data);
        const char* crc = protocol::alptas_crc_ok(frame.data) ? "crc ok" : "CRC BAD";
        return head + std::snprintf(out + head, left, "ALP-TAS %06X %s", address, crc);
    }

    protocol::AdslPacket packet{};
    packet.reset();
    std::memcpy(packet.data, frame.data, protocol::kAdslFrameBytes);
    const bool crc_ok = packet.check_crc() == 0;
    packet.descramble();
    return head + std::snprintf(out + head, left, "ADS-L %06X %+.5f,%+.5f %5d m %s",
                                packet.address(), packet.latitude_1e7() / 1e7,
                                packet.longitude_1e7() / 1e7, packet.altitude_m(),
                                crc_ok ? "crc ok" : "CRC BAD");
}

}  // namespace skyblip::simulator
