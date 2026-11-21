// Harness, not a test: slot0 as a .uf2 from Briefing leaves it, an image and its trust record.
#ifndef SKYBLIP_TEST_SUPPORT_INSTALL_TRUST_H
#define SKYBLIP_TEST_SUPPORT_INSTALL_TRUST_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "core/comms/install_trust.h"
#include "core/crypto/ed25519.h"
#include "core/util/crc32.h"

namespace skyblip {

class InstalledSlot {
   public:
    static constexpr uint32_t kSlotBytes = 0xB5000;
    static constexpr uint32_t kHeaderBytes = 0x200;
    static constexpr uint32_t kImageBytes = 0x6C1A4;
    static constexpr uint16_t kTlvTotal = 152;
    static constexpr uint32_t kPage = 0x6D000;

    static std::vector<uint8_t> record(const crypto::Ed25519::PublicKey& key,
                                       const std::string& label) {
        std::vector<uint8_t> out(comms::kInstallTrustRecordBytes, 0);
        std::memcpy(out.data(), comms::kInstallTrustMagic, comms::kInstallTrustMagicBytes);
        std::memcpy(out.data() + 16, key.data(), key.size());
        out[48] = static_cast<uint8_t>(label.size());
        std::memcpy(out.data() + 49, label.data(), label.size());
        put_uint32(out, 73, crc32(out.data(), 73));
        return out;
    }

    static std::vector<uint8_t> slot(const std::vector<uint8_t>& trust_record) {
        std::vector<uint8_t> out(kSlotBytes, 0xFF);
        put_uint32(out, 0, 0x96f3b83d);
        put_uint16(out, 8, kHeaderBytes);
        put_uint16(out, 10, 0);
        put_uint32(out, 12, kImageBytes);
        put_uint16(out, kHeaderBytes + kImageBytes, 0x6907);
        put_uint16(out, kHeaderBytes + kImageBytes + 2, kTlvTotal);
        std::memcpy(out.data() + kPage, trust_record.data(), trust_record.size());
        return out;
    }

   private:
    static void put_uint16(std::vector<uint8_t>& out, size_t at, uint32_t value) {
        out[at] = static_cast<uint8_t>(value);
        out[at + 1] = static_cast<uint8_t>(value >> 8);
    }
    static void put_uint32(std::vector<uint8_t>& out, size_t at, uint32_t value) {
        for (size_t i = 0; i < 4; i++) out[at + i] = static_cast<uint8_t>(value >> (8 * i));
    }
};

}  // namespace skyblip

#endif
