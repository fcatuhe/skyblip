// INFO: fc 15nov26 seven subsystems, adsb only on a 1090 MHz receiver, capture only with a ring
#ifndef SKYBLIP_CORE_COMMS_DIAGNOSTICS_H
#define SKYBLIP_CORE_COMMS_DIAGNOSTICS_H

#include <cstdint>

#include "core/comms/link_registry.h"
#include "core/gnss/validity.h"
#include "core/power/battery.h"
#include "core/power/charging.h"
#include "core/power/cutoff.h"
#include "core/power/reset_reason.h"
#include "core/timing/channel.h"
#include "core/timing/transmit_gate.h"
#include "ports/adsb_receiver.h"
#include "ports/gnss.h"
#include "protocols/adsb/message.h"

namespace skyblip::comms {

enum class CaptureProfile : uint8_t { Full = 0, PowerRun = 1, FlightRun = 2, Stop = 3 };

// INFO: fc 15nov26 the words the capture prompt on the glass names them by
const char* capture_profile_word(CaptureProfile profile);

// INFO: fc 20nov26 two writers, the product's collector and ConfigService, so no number has a copy
struct Diagnostics {
    // INFO: fc 20nov26 the link refuses the dump until this moves, the proof a collector is wired
    uint32_t refreshes{0};
    uint32_t uptime_s{0};

    int8_t noise_dbm{timing::NoiseFloor::kSeedDbm};
    uint32_t duty_permille{0};
    uint32_t rx_ok{0};
    uint32_t rx_bad{0};
    uint32_t rx_wait{0};
    uint32_t rx_type{0};
    uint32_t rx_unframed{0};
    uint32_t rx_miskeyed{0};
    uint32_t rx_noise{0};
    uint32_t rx_named{0};
    uint32_t tx_ok{0};
    uint32_t tx_lost{0};
    uint32_t tx_named{0};
    uint32_t tx_held_for_b5{0};
    uint32_t rx_status{0};
    uint32_t uplink_stale{0};
    uint32_t tx_given_up{0};
    uint32_t tx_deferred{0};
    uint32_t tx_forced{0};
    uint32_t tx_busy_missed{0};
    uint32_t tx_held_for_fix{0};
    uint32_t tx_redrawn_for_fix{0};
    timing::NoTx no_tx{timing::NoTx::None};
    uint16_t tx_keyed_us{0};
    uint16_t tx_span_us{0};
    bool dio1_armed{false};
    uint32_t dio1_edges{0};
    uint32_t dio1_missed{0};

    uint32_t tracked{0};
    uint8_t alarm{0};
    uint32_t uplink_frames{0};
    uint32_t uplink_bad{0};
    uint32_t uplink_targets{0};

    uint32_t gnss_fixes{0};
    bool fix_valid{false};
    uint32_t gnss_baud{0};
    ports::GnssConfigStage gnss_config{ports::GnssConfigStage::Idle};
    uint32_t gnss_raises{0};
    uint32_t gnss_raise_fallbacks{0};
    bool gnss_identified{false};
    // INFO: fc 20nov26 points at the driver's own buffer, which outlives every reader
    const char* gnss_firmware{""};
    gnss::FixReject gnss_reject{gnss::FixReject::None};
    uint32_t gnss_rejected{0};
    uint32_t gnss_overruns{0};
    uint16_t gnss_solution_phase_ms{0};
    bool gnss_solution_phase_valid{false};
    uint32_t gnss_recoveries{0};
    bool gnss_recoveries_valid{false};
    uint16_t residual_m{0};
    bool residual_valid{false};

    uint32_t supply_warnings{0};
    uint32_t battery_implausible{0};
    power::ChargeCondition charge{power::ChargeCondition::Unknown};
    uint32_t charge_warnings{0};

    bool adsb_fitted{false};
    ports::AdsbReceiverStatistics adsb{};
    uint32_t adsb_crc{0};
    uint32_t adsb_unsupported{0};
    uint32_t adsb_folded{0};
    uint32_t adsb_aircraft{0};
    uint32_t adsb_formats[adsb::kDownlinkFormats]{};
    uint32_t adsb_transponders{0};
    uint32_t adsb_mode_s{0};
    uint32_t adsb_mode_s_altitude{0};
    uint32_t adsb_unmatched{0};

    bool capture_available{false};
    bool capture_armed{false};
    CaptureProfile capture_profile{CaptureProfile::Full};
    uint32_t capture_session{0};
    uint32_t capture_records{0};
    uint32_t capture_dropped{0};
    uint32_t capture_sectors{0};
    uint32_t capture_keeps_s{0};
    uint32_t capture_takes_sectors{0};
    uint32_t capture_evicts_flights{0};
    uint32_t capture_keeps_full_s{0};
    uint32_t capture_keeps_power_run_s{0};

    power::ResetReason reset{power::ResetReason::Unknown};
    uint32_t link_drops{0};
    uint8_t link_drop_reason{0};
    uint32_t failed_writes{0};
    uint32_t range_refused{0};
    power::BatteryState battery{};
    power::PowerLevel power_level{power::PowerLevel::Unknown};
    int16_t die_dc{0};
    bool die_valid{false};
};

// INFO: fc 20nov26 rounded away from zero, and only here, so every reply rounds alike
int whole_celsius(int16_t temperature_dc);

class DiagnosticsReport {
   public:
    enum class Group : uint8_t { System, Radio, Traffic, Gnss, Power, Adsb, Capture };
    static constexpr int kGroupCount = 7;

    // INFO: fc 20nov26 a buffer bound only: a frame never outgrows the negotiated payload
    static constexpr int kFrameBytes = 384;
    // INFO: fc 19nov26 add() drops a field past this, so a frames test keeps the widest under it
    static constexpr int kMaxFields = 96;

    // INFO: fc 20oct26 command is what the reply answers under: radio asks for one group
    DiagnosticsReport(const Diagnostics& diagnostics, link::Command command);
    DiagnosticsReport(const Diagnostics& diagnostics, link::Command command, Group only);

    // INFO: fc 20oct26 whole maps with command, group, part and more, never two groups in one
    bool fits(int payload_bytes) const;
    bool exhausted() const { return at_ >= count_; }
    int next_frame(int payload_bytes, uint8_t* out, int capacity);

   private:
    enum class Kind : uint8_t { Unsigned, Integer, Boolean, Text, UnsignedArray };
    enum class Array : uint8_t { AdsbRssi, AdsbFormats };

    // INFO: fc 21nov26 every value fits 32 bits and every key a byte, so a field is 8 bytes on
    // silicon
    struct Field {
        uint8_t key;
        Kind kind;
        Group group;
        union {
            uint32_t value;
            int32_t integer;
            const char* text;
        };
    };

    void build(const Diagnostics& diagnostics, const Group* only);
    void add_capture(const Diagnostics& diagnostics);
    Field* add(Group group, uint8_t key, Kind kind, uint32_t value);
    void add_integer(Group group, uint8_t key, int32_t value);
    void add_text(Group group, uint8_t key, const char* text);
    void add_array(Group group, uint8_t key, Array array);
    const uint32_t* values_of(const Field& field, int& count) const;
    int field_bytes(const Field& field) const;

    link::Command command_;
    // INFO: fc 11nov26 array fields read these copies, so a refresh never changes a dump being sent
    static constexpr int kAdsbRssiCount = ports::AdsbReceiverStatistics::kRssiBuckets;
    uint32_t adsb_rssi_[kAdsbRssiCount]{};
    uint32_t adsb_formats_[adsb::kDownlinkFormats]{};
    Field fields_[kMaxFields]{};
    int count_{0};
    int at_{0};
    int part_{0};
};

// INFO: fc 05aug26 refreshed every second, because the link may ask at any instant
constexpr uint32_t kDiagnosticsRefreshPeriodMs = 1000;

}  // namespace skyblip::comms

#endif
