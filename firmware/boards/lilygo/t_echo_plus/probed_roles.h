#ifndef SKYBLIP_BOARDS_T_ECHO_PLUS_PROBED_ROLES_H
#define SKYBLIP_BOARDS_T_ECHO_PLUS_PROBED_ROLES_H

#include "ports/annunciator.h"
#include "ports/rf.h"

namespace skyblip::boards::t_echo_plus {

// INFO: fc 11nov26 the product takes roles before probe(): each is its null part until proven
class ProbedRf : public ports::Rf {
   public:
    explicit ProbedRf(ports::Rf& absent) : part_(&absent) {}

    void prove(ports::Rf& part) { part_ = &part; }

    Status begin() override { return part_->begin(); }
    Status arm(const ports::RfPlan& plan) override { return part_->arm(plan); }
    void abort() override { part_->abort(); }
    Status withdraw(uint64_t tx_at_us) override { return part_->withdraw(tx_at_us); }
    ports::RfCarrier carrier() const override { return part_->carrier(); }
    ports::RfSwitching switching() const override { return part_->switching(); }
    ports::RfTransmitter transmitter() const override { return part_->transmitter(); }
    ports::RfDio1 dio1() const override { return part_->dio1(); }
    void sleep() override { part_->sleep(); }
    bool parked() const override { return part_->parked(); }
    void abandon() override { part_->abandon(); }

   private:
    ports::Rf* part_;
};

class ProbedAnnunciator : public ports::Annunciator {
   public:
    explicit ProbedAnnunciator(ports::Annunciator& absent) : part_(&absent) {}

    void prove(ports::Annunciator& part) { part_ = &part; }

    void tone(uint16_t pitch_hz, ports::Loudness loudness) override {
        part_->tone(pitch_hz, loudness);
    }
    void silence() override { part_->silence(); }
    void vibrate() override { part_->vibrate(); }
    void still() override { part_->still(); }

   private:
    ports::Annunciator* part_;
};

}  // namespace skyblip::boards::t_echo_plus

#endif
