#include "products/skyblip_go/services/nmea.h"

#include <cstring>

#include "core/events/link.h"
#include "core/flight/atmosphere.h"
#include "core/flight/extrapolate.h"
#include "core/model/ownship.h"
#include "core/protocol/nmea_out.h"
#include "core/timing/transmit.h"
#include "core/util/intmath.h"

namespace skyblip::go {

namespace {

// 1 ft = 30.48 cm, and the altitude arrives in centimetres, so the division is
// by 3048 hundredths of a centimetre with the half-foot carried in by hand.
constexpr int32_t kHundredthCmPerFoot = 3048;
constexpr int32_t kHalfFootHundredthCm = kHundredthCmPerFoot / 2;

int32_t centimetres_to_feet(int32_t alt_cm) {
    const int32_t hundredths = alt_cm * 100;
    return (hundredths >= 0 ? hundredths + kHalfFootHundredthCm
                            : hundredths - kHalfFootHundredthCm) /
           kHundredthCmPerFoot;
}

// INFO: nm 04aug26 $PFLAU's relative bearing is signed, half a turn either way:
// a threat 20 degrees off the left wing is -20 and not 340, and an app that
// draws the arrow from it puts the same threat on the wrong side otherwise.
// core/traffic assesses in whole degrees clockwise, so the wrap is the sender's
// to make - it is the one both SoftRF forks make on the same field
// (oss/SoftRF-lyusupov/.../protocol/data/NMEA.cpp, rel_bearing).
constexpr uint16_t kHalfTurnDeg = 180;
constexpr int32_t kFullTurnDeg = 360;

int32_t signed_bearing(uint16_t clockwise_deg) {
    const int32_t deg = static_cast<int32_t>(clockwise_deg);
    return deg > kHalfTurnDeg ? deg - kFullTurnDeg : deg;
}

}  // namespace

Status NmeaService::setup() {
    enabled_ =
        has_feature(supported(declared_, context_.roles.capabilities), Feature::CompanionLink);
    return Status::Ok;
}

// INFO: nm 04aug26 Nobody listening costs one boolean and a null check: no walk
// of the traffic table, no formatting, nothing handed to a link that would
// refuse it. That matters because this runs in the same pass as the dwell map,
// and because the common case in a club is a device flying with no phone paired
// at all.
bool NmeaService::listening() const {
    if (!enabled_) return false;
    return config_.link_up();
}

void NmeaService::tick(uint32_t now_ms) {
    if (!listening()) {
        // A tablet that comes back is a tablet that gets the whole table again,
        // starting from the top, on its first pass.
        cursor_ = 0;
        passed_once_ = false;
        pass_len_ = pass_sent_ = resume_count_ = 0;
        return;
    }
    drain();
    if (passed_once_) {
        const uint32_t since = now_ms - last_pass_ms_;
        if (since < kMovingTargetRedrawMs) return;
        if (context_.state.rf.dwell.burst_armed &&
            since < kMovingTargetRedrawMs + kPassDeferralCeilingMs)
            return;
    }
    last_pass_ms_ = now_ms;
    passed_once_ = true;
    run_pass(now_ms);
}

// The order is the policy: the alarm sentence is written first and is never
// inside the per-pass cap, so the one sentence a pilot's life may depend on
// cannot be the one a full sky pushes off the end.
void NmeaService::run_pass(uint32_t now_ms) {
    const int negotiated = static_cast<int>(context_.roles.link.payload_bytes());
    payload_ = negotiated < ports::kMinimumLinkPayload ? ports::kMinimumLinkPayload
               : negotiated > kFrameBytesCap           ? kFrameBytesCap
                                                       : negotiated;
    if (pass_sent_ < pass_len_) abandon_pass();
    pass_len_ = pass_sent_ = resume_count_ = 0;
    emit_status(now_ms);
    emit_ownship();
    emit_altitude();
    emit_vario_and_battery();
    emit_targets(now_ms);
    drain();
}

// $PFLAU: how many we hear, whether we have a fix, and the one contact that
// matters most - highest level, nearest at equal level. The level is the one
// core/traffic already published on the target (products/.../alarm.cpp is its
// single writer), so the tablet's alarm and the buzzer cannot disagree.
void NmeaService::emit_status(uint32_t now_ms) {
    const model::OwnState& own = context_.state.own;
    const traffic::Target* threat = nullptr;
    traffic::AlarmAssessment worst{};
    traffic::Level worst_level = traffic::Level::None;
    int heard = 0;

    for (int slot = 0; slot < traffic::TrafficTable::kCapacity; slot++) {
        const traffic::Target* target = context_.state.traffic.at(slot);
        if (target == nullptr || !target->used) continue;
        heard++;
        const traffic::AlarmAssessment assessment = traffic::assess(own, target->obs, now_ms);
        if (!assessment.valid) continue;
        const bool higher = target->alarm_level > worst_level;
        const bool nearer = target->alarm_level == worst_level &&
                            (threat == nullptr || assessment.rel_dist_m < worst.rel_dist_m);
        if (!higher && !nearer) continue;
        threat = target;
        worst = assessment;
        worst_level = target->alarm_level;
    }

    const bool transmitting = timing::own_ship_transmits(own, context_.state.clock);
    const int len =
        threat != nullptr
            ? protocol::format_pflau(sentence_, sizeof(sentence_), own, transmitting, heard,
                                     &threat->obs, traffic::to_number(worst_level),
                                     static_cast<int16_t>(signed_bearing(worst.rel_bearing_deg)),
                                     worst.rel_alt_m, worst.rel_dist_m)
            : protocol::format_pflau(sentence_, sizeof(sentence_), own, transmitting, heard,
                                     nullptr, 0, 0, 0, 0);
    write(sentence_, len);
}

// $GPRMC/$GPGGA: ownship's own absolute position, for the EFB that has none of
// its own - a panel-mounted tablet with no sky view is exactly the case this
// closes, and it is the one sentence pair every EFB in project/reference/efb-
// formats.md accepts unconditionally. Both are one call each, gated inside
// core/protocol on the same fix and UTC validity $PGRMZ and $PFLAU already
// read off own, so there is nothing to gate here a second time.
void NmeaService::emit_ownship() {
    const model::OwnState& own = context_.state.own;
    const int rmc = protocol::format_gprmc(sentence_, sizeof(sentence_), own);
    if (rmc > 0) write(sentence_, rmc);
    const int gga = protocol::format_gpgga(sentence_, sizeof(sentence_), own);
    if (gga > 0) write(sentence_, gga);
}

// INFO: nm 04aug26 $PGRMZ is read as a BAROMETRIC altitude and the app applies
// its own subscale to it (XCSoar hands it straight to its pressure-altitude
// input), so what goes out is pressure altitude on 1013.25 hPa - the datum-free
// figure core/flight/atmosphere.h exists to produce. Sending
// bus::State::qnh_pa-corrected altitude instead would be corrected a second time
// on the tablet and read hundreds of feet out on a low-pressure day; sending the
// GNSS altitude under this sentence name would feed a geometric height into an
// app's altimeter and vario. With no barometer contributing there is no such
// figure and the sentence is not sent at all, which leaves the tablet on its own
// GNSS altitude - both SoftRF forks gate the same sentence on a baro chip being
// present for the same reason.
void NmeaService::emit_altitude() {
    if (!context_.state.baro.active) return;
    const int32_t alt_cm =
        flight::pressure_to_alt_cm(div_round<uint32_t>(context_.state.baro.pressure_mpa, 1000));
    write(sentence_,
          protocol::format_pgrmz(sentence_, sizeof(sentence_), centimetres_to_feet(alt_cm),
                                 context_.state.own.fix_valid));
}

// $LK8EX1, on the same pass as $PGRMZ because it is the same second's answer to
// the same question, and this is the whole route from our gauge to a pilot's
// tablet: LK8000, XCSoar and their descendants already parse it, and none of
// them learns the battery from $PFLAU or $PGRMZ. Unlike $PGRMZ it goes out with
// no barometer fitted at all, carrying the sentinels for the three fields that
// need one and the cell for the field that does not - the sentence is the only
// one we speak that says anything about power, so silence here is a pilot with
// no way to see a flat unit coming (SoftRF MB sends exactly the same
// battery-only sentence in that case, src/protocol/data/NMEA.cpp:1398-1401).
//
// INFO: cc 19sep26 the BME280 sits inside the case, so field 4 goes out warm and uncorrected
void NmeaService::emit_vario_and_battery() {
    const model::OwnState& own = context_.state.own;
    protocol::Lk8Ex1 v{};

    if (context_.state.baro.active) {
        v.pressure_pa = div_round<uint32_t>(context_.state.baro.pressure_mpa, 1000);
        v.pressure_valid = true;
        // Field 2 is the 1013.25 datum, the same datum-free figure $PGRMZ
        // carries and for the same reason: the consumer applies its own
        // subscale. A consumer that read field 1 recomputes this and ignores it.
        v.alt_m = div_round(
            flight::pressure_to_alt_cm(div_round<uint32_t>(context_.state.baro.pressure_mpa, 1000)),
            100);
        v.alt_valid = true;
    }

    if (context_.state.baro.temperature_valid) {
        v.temperature_c = div_round<int32_t>(context_.state.baro.temperature_decicelsius, 10);
        v.temperature_valid = true;
    }

    if (own.climb_valid) {
        const int32_t mm_s = own.climb_mm_s;
        v.vario_cm_s = (mm_s >= 0 ? mm_s + 5 : mm_s - 5) / 10;
        v.vario_valid = true;
    }

    v.battery_percent = context_.state.power.battery.percent;
    v.battery_valid = context_.state.power.battery.valid;

    write(sentence_, protocol::format_lk8ex1(sentence_, sizeof(sentence_), v));
}

// The rotation: at most kTargetsPerPass targets, resuming where the last pass
// stopped, wrapping the table. A busier sky than one pass can carry costs
// latency on the tail and never a target - the alternative, a burst that sends
// the first N slots every second, means slot 11 is a target the tablet is never
// told about at all.
void NmeaService::emit_targets(uint32_t now_ms) {
    const model::OwnState own = flight::carried_to(context_.state.own, now_ms);
    if (!own.fix_valid) return;

    int sent = 0;
    const int from = cursor_;
    for (int step = 0; step < traffic::TrafficTable::kCapacity && sent < kTargetsPerPass; step++) {
        const int slot = (from + step) % traffic::TrafficTable::kCapacity;
        const traffic::Target* target = context_.state.traffic.at(slot);
        if (target == nullptr || !target->used) continue;
        const int len = protocol::format_pflaa(
            sentence_, sizeof(sentence_), own, flight::carried_to(target->obs, now_ms),
            traffic::to_number(target->alarm_level),
            context_.state.callsigns.find(target->obs.addr_table, target->obs.addr));
        if (len <= 0) continue;
        write(sentence_, len);
        resumes_[resume_count_++] = {pass_len_, (slot + 1) % traffic::TrafficTable::kCapacity};
        sent++;
    }
}

// INFO: nm 04aug26 A frame is a slice of a byte stream, not a sentence: the
// widest $PFLAA these targets can produce is longer than the 20 bytes BLE
// guarantees (test/core/test_nmea_out.cpp pins exactly that), so one sentence
// per frame would send a phone at the default ATT_MTU nothing at all - the very
// silence this service exists to end. NMEA is line-delimited and every consumer
// resynchronises on CRLF, which is why both SoftRF forks push their whole NMEA
// output through a 20-byte BLE chunker. The cost is round trips, not integrity:
// a measured full-table pass is 549 bytes, which is three notifications at a
// negotiated 244, four at an iPhone's 182 and twenty-eight at the guaranteed 20.
// INFO: fc 25sep26 more than the link's notify share, so the pass is held here and drained per tick
void NmeaService::write(const char* bytes, int len) {
    if (len <= 0 || pass_len_ + len > kPassBytesCap) return;
    std::memcpy(pass_ + pass_len_, bytes, static_cast<size_t>(len));
    pass_len_ += len;
}

// A link that refuses outright ends the pass rather than being argued with: what
// is left unsent is a second-old picture, and the next pass has a current one.
void NmeaService::drain() {
    while (pass_sent_ < pass_len_) {
        const int rest = pass_len_ - pass_sent_;
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
    pass_len_ = pass_sent_ = 0;
}

}  // namespace skyblip::go
