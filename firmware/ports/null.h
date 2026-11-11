#ifndef SKYBLIP_PORTS_NULL_H
#define SKYBLIP_PORTS_NULL_H

#include "core/events/link.h"
#include "ports/annunciator.h"
#include "ports/central.h"
#include "ports/dfu.h"
#include "ports/die_temperature.h"
#include "ports/display.h"
#include "ports/flash_region.h"
#include "ports/gnss.h"
#include "ports/identity.h"
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
    void tone(uint16_t, Loudness) override {}
    void silence() override {}
    void vibrate() override {}
    void still() override {}
};

class NullLink : public Link {
   public:
    Status send(events::Endpoint, ConstByteSpan) override { return Status::Down; }
    Status send_to(const events::Peer&, events::Endpoint, ConstByteSpan) override {
        return Status::Down;
    }
    Status advertise_as(const char*) override { return Status::Down; }
    Status reserve_peer_connection(bool) override { return Status::Down; }
};

class NullCentral : public Central {
   public:
    Status scan(const ScanDuty&, const char*) override { return Status::Down; }
    void stop_scan() override {}
    Status connect(const events::BluetoothAddress&, const ScanDuty&) override {
        return Status::Down;
    }
    void disconnect() override {}
    Status write(ConstByteSpan) override { return Status::Down; }
};

class NullKeyValueStore : public KeyValueStore {
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

// INFO: fc 12nov26 a unit that is never activated: the FICR tag names it, and it holds no key
class KeylessIdentity : public Identity {
   public:
    explicit KeylessIdentity(uint16_t bluetooth_tag) : bluetooth_tag_(bluetooth_tag) {}

    identity::DeviceId device_id() const override { return identity::DeviceId{}; }
    uint16_t bluetooth_tag() const override { return bluetooth_tag_; }
    bool seed(crypto::Ed25519::Seed&) const override { return false; }
    const crypto::Ed25519::PublicKey& trusted_server_key() const override { return kNoServerKey; }
    Status read_unlock_code(identity::UnlockCode&) override { return Status::NotFound; }
    Status write_unlock_code(const identity::UnlockCode&) override { return Status::Unsupported; }
    bool draw_random(ByteSpan) override { return false; }

   private:
    static constexpr crypto::Ed25519::PublicKey kNoServerKey{};

    uint16_t bluetooth_tag_;
};

struct NullRoles {
    NullDisplay display;
    NullAnnunciator annunciator;
    NullLink link;
    NullCentral central;
    NullKeyValueStore key_value;
    NullFlashRegion log_flash;
    NullDfu dfu;
    NullRf rf;
    DieTemperature die_temperature;
    Indicator indicator;
    Gnss gnss;
};

}  // namespace skyblip::ports

#endif
