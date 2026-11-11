#include "products/common/services/adsb_capture.h"

#include "core/diagnostics/payload.h"

namespace skyblip::common {

static_assert(AdsbCapture::kBudgetCeilingRecords * AdsbCapture::kRecordPeriodMs /
                      AdsbCapture::kBudgetWindowMs <=
                  static_cast<uint32_t>(AdsbCapture::kAircraft),
              "the budget lets more aircraft through a period than the table remembers");

bool AdsbCapture::record(const model::AircraftObservation& observation, uint32_t now_ms) {
    if (observation.source != model::Source::Adsb || !context_.diagnostics.armed()) return false;
    Recorded* last = find(observation.address);
    if (last != nullptr && now_ms - last->at_ms < kRecordPeriodMs) return false;
    if (last == nullptr) last = vacancy(now_ms);
    if (last == nullptr || !affordable(now_ms)) {
        refused_++;
        return false;
    }
    if (!context_.diagnostics.record(diagnostics::adsb_of(context_.state.own, observation, now_ms),
                                     context_.instant(now_ms)))
        return false;
    if (last == recorded_.data() + held_) held_++;
    last->address = observation.address;
    last->at_ms = now_ms;
    spend(now_ms);
    return true;
}

bool AdsbCapture::affordable(uint32_t now_ms) const {
    return spent_ < kBudgetCeilingRecords ||
           now_ms - spent_at_ms_[oldest_spent_] >= kBudgetWindowMs;
}

void AdsbCapture::spend(uint32_t now_ms) {
    spent_at_ms_[oldest_spent_] = now_ms;
    oldest_spent_ = (oldest_spent_ + 1) % kBudgetCeilingRecords;
    if (spent_ < kBudgetCeilingRecords) spent_++;
}

AdsbCapture::Recorded* AdsbCapture::find(uint32_t address) {
    for (int i = 0; i < held_; i++)
        if (recorded_[i].address == address) return &recorded_[i];
    return nullptr;
}

AdsbCapture::Recorded* AdsbCapture::vacancy(uint32_t now_ms) {
    if (held_ < kAircraft) return &recorded_[held_];
    for (Recorded& entry : recorded_)
        if (now_ms - entry.at_ms >= kRecordPeriodMs) return &entry;
    return nullptr;
}

}  // namespace skyblip::common
