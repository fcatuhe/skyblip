#ifndef SKYBLIP_PORTS_NULL_H
#define SKYBLIP_PORTS_NULL_H

#include "core/events/link.h"
#include "ports/annunciator.h"
#include "ports/dfu.h"
#include "ports/die_temperature.h"
#include "ports/display.h"
#include "ports/flash_region.h"
#include "ports/gnss.h"
#include "ports/indicator.h"
#include "ports/kvstore.h"
#include "ports/link.h"
#include "ports/rf.h"

namespace skyblip::ports {

class NullDisplay : public Display {
   public:
    void present(const ui::Canvas&, Refresh, uint32_t) override {}
    void power_off() override {}
};

class NullAnnunciator : public Annunciator {
   public:
    void alarm(uint8_t, uint8_t) override {}
    void tone(uint16_t, uint8_t) override {}
    void vibrate(uint16_t) override {}
    void silence() override {}
};

class NullLink : public Link {
   public:
    Status send(events::Endpoint, ConstByteSpan) override { return Status::Down; }
    Status send_to(uint16_t, events::Endpoint, ConstByteSpan) override { return Status::Down; }
};

class NullKvStore : public KvStore {
   public:
    bool ready() const override { return false; }
    Status read(const char*, uint8_t*, size_t, size_t&) override { return Status::NotFound; }
    Status write(const char*, const uint8_t*, size_t) override { return Status::Down; }
    Status erase(const char*) override { return Status::Ok; }
};

class NullFlashRegion : public FlashRegion {
   public:
    bool ready() const override { return false; }
    uint32_t sector_bytes() const override { return 0; }
    uint32_t sector_count() const override { return 0; }
    Status read(uint32_t, uint8_t*, uint32_t) override { return Status::Down; }
    Status write(uint32_t, const uint8_t*, uint32_t) override { return Status::Down; }
    Status erase_sector(uint32_t) override { return Status::Down; }
};

class NullDfu : public Dfu {
   public:
    void trigger() override {}
};

class NullRf : public Rf {
   public:
    Status begin() override { return Status::Down; }
    Status arm(const RfPlan&) override { return Status::Down; }
    void abort() override {}
};

struct NullRoles {
    NullDisplay display;
    NullAnnunciator annunciator;
    NullLink link;
    NullKvStore kv;
    NullFlashRegion log_flash;
    NullDfu dfu;
    NullRf rf;
    DieTemperature die_temperature;
    Indicator indicator;
    Gnss gnss;
};

}  // namespace skyblip::ports

#endif
