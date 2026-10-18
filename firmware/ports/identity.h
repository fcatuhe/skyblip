#ifndef SKYBLIP_PORTS_IDENTITY_H
#define SKYBLIP_PORTS_IDENTITY_H

#include <cstdint>

#include "core/crypto/ed25519.h"
#include "core/identity/messages.h"
#include "core/identity/unlock_code.h"
#include "core/util/result.h"
#include "core/util/span.h"

namespace skyblip::ports {

class Identity {
   public:
    virtual ~Identity() = default;

    virtual identity::DeviceId device_id() const = 0;
    virtual uint16_t bluetooth_tag() const = 0;
    virtual bool seed(crypto::Ed25519::Seed& out) const = 0;
    virtual const crypto::Ed25519::PublicKey& trusted_server_key() const = 0;

    virtual Status read_unlock_code(identity::UnlockCode& out) = 0;
    virtual Status write_unlock_code(const identity::UnlockCode& code) = 0;

    virtual bool draw_random(ByteSpan out) = 0;
};

}  // namespace skyblip::ports

#endif
