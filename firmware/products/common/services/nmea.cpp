#include "products/common/services/nmea.h"

#include <cstring>

#include "core/events/link.h"
#include "core/flight/atmosphere.h"
#include "core/flight/extrapolate.h"
#include "core/model/ownship.h"
#include "core/protocol/nmea_out.h"
#include "core/timing/transmit.h"
#include "protocols/common/geometry.h"

namespace skyblip::common {

namespace {

constexpr int32_t kHundredthCmPerFoot = 3048;
constexpr int32_t kHalfFootHundredthCm = kHundredthCmPerFoot / 2;

int32_t centimetres_to_feet(int32_t altitude_cm) {
    const int32_t hundredths = altitude_cm * 100;
    return (hundredths >= 0 ? hundredths + kHalfFootHundredthCm
                            : hundredths - kHalfFootHundredthCm) /
           kHundredthCmPerFoot;
}

// INFO: nm 04aug26 $PFLAU's relative bearing is signed, so a threat 20 degrees off the left wing is
// -20 and not 340
constexpr uint16_t kHalfTurnDeg = 180;
constexpr int32_t kFullTurnDeg = 360;

int32_t signed_bearing(uint16_t clockwise_deg) {
    const int32_t bearing_deg = static_cast<int32_t>(clockwise_deg);
    return bearing_deg > kHalfTurnDeg ? bearing_deg - kFullTurnDeg : bearing_deg;
}

}  // namespace

Status NmeaService::setup() {
    enabled_ = ports::has(context_.roles.capabilities, ports::Capability::Link);
    return Status::Ok;
}

// INFO: nm 04aug26 nobody listening costs one boolean, since this runs in the dwell map's pass and
// most units fly with no phone
bool NmeaService::listening() const {
    if (!enabled_) return false;
    return config_.link_up();
}

void NmeaService::tick(uint32_t now_ms) {
    if (!listening()) {
        cursor_ = 0;
        passed_once_ = false;
        pass_bytes_ = pass_sent_ = resume_count_ = 0;
        return;
    }
    drain();
    note_whole_second(now_ms);
    if (passed_once_ && !pass_due(now_ms)) return;
    last_pass_ms_ = now_ms;
    passed_once_ = true;
    passed_second_s_ = landed_second_s_;
    run_pass(now_ms);
}

uint32_t NmeaService::due_in_ms(uint32_t now_ms) const {
    if (!listening()) return runtime::kServiceRestCeilingMs;
    if (!passed_once_ || pass_sent_ < pass_bytes_) return runtime::kServicePassCeilingMs;
    const uint32_t since_ms = now_ms - last_pass_ms_;
    const uint32_t period_ms =
        landed_second_s_ != passed_second_s_ ? kPassGapFloorMs : cadence_ms();
    const uint32_t wait_ms = runtime::remaining_ms(since_ms, period_ms);
    return wait_ms == 0 ? runtime::kServicePassCeilingMs : wait_ms;
}

void NmeaService::note_whole_second(uint32_t now_ms) {
    const model::Ownship& second = context_.state.own_second;
    if (!second.fix_valid || !second.utc_valid || second.utc_s == landed_second_s_) return;
    landed_second_s_ = second.utc_s;
    landed_at_ms_ = now_ms;
}

// INFO: fc 25nov26 an armed burst holds the pass as before, at most kPassDeferralCeilingMs
bool NmeaService::pass_due(uint32_t now_ms) const {
    const bool burst_armed = context_.state.rf.dwell.burst_armed;
    const uint32_t since_ms = now_ms - last_pass_ms_;
    if (landed_second_s_ != passed_second_s_) {
        if (since_ms < kPassGapFloorMs) return false;
        return !burst_armed || now_ms - landed_at_ms_ >= kPassDeferralCeilingMs;
    }
    if (since_ms < cadence_ms()) return false;
    return !burst_armed || since_ms >= cadence_ms() + kPassDeferralCeilingMs;
}

uint32_t NmeaService::cadence_ms() const {
    const model::Ownship& second = context_.state.own_second;
    if (second.fix_valid && second.utc_valid)
        return kMovingTargetRedrawPeriodMs + kWholeSecondWaitMs;
    return kMovingTargetRedrawPeriodMs;
}

// INFO: fc 20nov26 the alarm sentence goes first and outside the per-pass cap, so a full sky never
// pushes it off the end
void NmeaService::run_pass(uint32_t now_ms) {
    const int negotiated = static_cast<int>(context_.roles.link.payload_bytes());
    payload_ = negotiated < ports::kMinimumLinkPayloadBytes ? ports::kMinimumLinkPayloadBytes
               : negotiated > kFrameBytes                   ? kFrameBytes
                                                            : negotiated;
    if (pass_sent_ < pass_bytes_) abandon_pass();
    pass_bytes_ = pass_sent_ = resume_count_ = 0;
    emit_status(now_ms);
    emit_ownship();
    emit_altitude();
    emit_vario_and_battery();
    emit_targets(now_ms);
    drain();
}

// INFO: fc 20nov26 the level is the one core/traffic published, so the tablet's alarm and the
// buzzer cannot disagree
void NmeaService::emit_status(uint32_t now_ms) {
    const model::Ownship& own = context_.state.own;
    const traffic::Target* threat = nullptr;
    traffic::AlarmAssessment worst{};
    traffic::Level worst_level = traffic::Level::None;
    int heard = 0;

    for (int slot = 0; slot < traffic::TrafficTable::kMaxTargets; slot++) {
        const traffic::Target* target = context_.state.traffic.at(slot);
        if (target == nullptr || !target->used) continue;
        heard++;
        const traffic::AlarmAssessment assessment =
            traffic::assess(own, target->observation, now_ms);
        if (!assessment.valid) continue;
        const bool higher = target->alarm_level > worst_level;
        const bool nearer =
            target->alarm_level == worst_level &&
            (threat == nullptr || assessment.relative_distance_m < worst.relative_distance_m);
        if (!higher && !nearer) continue;
        threat = target;
        worst = assessment;
        worst_level = target->alarm_level;
    }

    const bool transmitting = context_.state.transmit.transmits();
    const int length = threat != nullptr
                           ? protocol::format_pflau(
                                 sentence_, sizeof(sentence_), own, transmitting, heard,
                                 &threat->observation, traffic::to_number(worst_level),
                                 static_cast<int16_t>(signed_bearing(worst.relative_bearing_deg)),
                                 worst.relative_altitude_m, worst.relative_altitude_valid,
                                 worst.relative_distance_m)
                           : protocol::format_pflau(sentence_, sizeof(sentence_), own, transmitting,
                                                    heard, nullptr, 0, 0, 0, false, 0);
    write(sentence_, length);
}

// INFO: fc 20nov26 core/protocol gates both sentences on the fix and UTC validity, so nothing is
// gated here a second time
void NmeaService::emit_ownship() {
    const model::Ownship& own = context_.state.own_second;
    const int rmc = protocol::format_gprmc(sentence_, sizeof(sentence_), own);
    if (rmc > 0) write(sentence_, rmc);
    const int gga = protocol::format_gpgga(sentence_, sizeof(sentence_), own);
    if (gga > 0) write(sentence_, gga);
}

// INFO: nm 04aug26 $PGRMZ is read as barometric, so it carries pressure altitude on 1013.25 hPa and
// is not sent without a barometer
void NmeaService::emit_altitude() {
    if (!context_.state.barometer.active) return;
    const int32_t altitude_cm = flight::pressure_to_altitude_cm(
        wire::divide_rounded<uint32_t>(context_.state.barometer.pressure_mpa, 1000));
    write(sentence_,
          protocol::format_pgrmz(sentence_, sizeof(sentence_), centimetres_to_feet(altitude_cm),
                                 context_.state.own.fix_valid));
}

// INFO: fc 20nov26 sent without a barometer too, because it is the only sentence that carries the
// battery to an EFB INFO: cc 19sep26 the BME280 sits inside the case, so field 4 goes out warm and
// uncorrected
void NmeaService::emit_vario_and_battery() {
    const model::Ownship& own = context_.state.own;
    protocol::Lk8Ex1 vario{};

    if (context_.state.barometer.active) {
        vario.pressure_pa =
            wire::divide_rounded<uint32_t>(context_.state.barometer.pressure_mpa, 1000);
        vario.pressure_valid = true;
        // INFO: fc 20nov26 field 2 is pressure altitude on 1013.25 hPa, as in $PGRMZ, because the
        // consumer applies its own subscale
        vario.altitude_m =
            wire::divide_rounded(flight::pressure_to_altitude_cm(wire::divide_rounded<uint32_t>(
                                     context_.state.barometer.pressure_mpa, 1000)),
                                 100);
        vario.altitude_valid = true;
    }

    if (context_.state.barometer.temperature_valid) {
        vario.temperature_c =
            wire::divide_rounded<int32_t>(context_.state.barometer.temperature_dc, 10);
        vario.temperature_valid = true;
    }

    if (own.climb_valid) {
        const int32_t climb_mm_s = own.climb_mm_s;
        vario.vario_cm_s = (climb_mm_s >= 0 ? climb_mm_s + 5 : climb_mm_s - 5) / 10;
        vario.vario_valid = true;
    }

    vario.battery_percent = context_.state.power.battery.percent;
    vario.battery_valid = context_.state.power.battery.valid;

    write(sentence_, protocol::format_lk8ex1(sentence_, sizeof(sentence_), vario));
}

// INFO: fc 20nov26 the rotation resumes where the last pass stopped, so a busy sky costs the tail
// latency and never a target
void NmeaService::emit_targets(uint32_t now_ms) {
    const model::Ownship own = flight::carried_to(context_.state.own, now_ms);
    if (!own.fix_valid) return;

    int sent = 0;
    const int from = cursor_;
    for (int step = 0; step < traffic::TrafficTable::kMaxTargets && sent < kTargetsPerPass;
         step++) {
        const int slot = (from + step) % traffic::TrafficTable::kMaxTargets;
        const traffic::Target* target = context_.state.traffic.at(slot);
        if (target == nullptr || !target->used) continue;
        const int length = protocol::format_pflaa(
            sentence_, sizeof(sentence_), own, flight::carried_to(target->observation, now_ms),
            traffic::to_number(target->alarm_level),
            context_.state.callsigns.find(target->observation.address_mapping_table,
                                          target->observation.address));
        if (length <= 0) continue;
        write(sentence_, length);
        resumes_[resume_count_++] = {pass_bytes_, (slot + 1) % traffic::TrafficTable::kMaxTargets};
        sent++;
    }
}

// INFO: nm 04aug26 a frame is a slice of the byte stream, not a sentence, because a $PFLAA is wider
// than BLE's guaranteed 20 bytes INFO: fc 25sep26 more than the link's notify share, so the pass is
// held here and drained per tick
void NmeaService::write(const char* sentence, int bytes) {
    if (bytes <= 0 || pass_bytes_ + bytes > kPassBytes) return;
    std::memcpy(pass_ + pass_bytes_, sentence, static_cast<size_t>(bytes));
    pass_bytes_ += bytes;
}

// INFO: fc 20nov26 a refusing link ends the pass, because the next pass carries a current picture
void NmeaService::drain() {
    while (pass_sent_ < pass_bytes_) {
        const int rest = pass_bytes_ - pass_sent_;
        const int take = rest < payload_ ? rest : payload_;
        const Status sent = context_.roles.link.send(
            events::Endpoint::Nmea,
            ConstByteSpan(reinterpret_cast<const uint8_t*>(pass_) + pass_sent_,
                          static_cast<size_t>(take)));
        if (sent == Status::WouldBlock) return;
        if (!is_ok(sent)) {
            abandon_pass();
            return;
        }
        pass_sent_ += take;
        for (int i = 0; i < resume_count_ && resumes_[i].end <= pass_sent_; i++)
            cursor_ = resumes_[i].cursor;
    }
}

void NmeaService::abandon_pass() {
    link_drops_++;
    pass_bytes_ = pass_sent_ = 0;
}

}  // namespace skyblip::common
