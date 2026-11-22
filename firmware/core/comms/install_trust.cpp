#include "core/comms/install_trust.h"

#include <algorithm>
#include <cstring>

#include "core/dfu/image.h"
#include "core/util/cbor.h"
#include "core/util/crc32.h"
#include "protocols/common/little_endian.h"

namespace skyblip::comms {

namespace {

constexpr size_t kKeyAt = kInstallTrustMagicBytes;
constexpr size_t kLabelBytesAt = kKeyAt + crypto::Ed25519::kPublicKeyBytes;
constexpr size_t kLabelAt = kLabelBytesAt + 1;
constexpr size_t kCrcAt = kLabelAt + kBrowserLabelBytes;

bool zero(ConstByteSpan bytes) {
    return std::all_of(bytes.begin(), bytes.end(), [](uint8_t byte) { return byte == 0; });
}

std::optional<uint32_t> running_image_end(ports::Dfu& slots) {
    uint8_t header_bytes[dfu::kImageHeaderBytes];
    dfu::ImageHeader header;
    if (!slots.read_running_slot(0, header_bytes, sizeof(header_bytes)) ||
        !dfu::read_header(header_bytes, sizeof(header_bytes), header))
        return std::nullopt;
    uint8_t info[dfu::kTlvInfoBytes];
    if (!slots.read_running_slot(header.unprotected_tlv_offset(), info, sizeof(info)))
        return std::nullopt;
    return dfu::image_end(header, info, sizeof(info));
}

}  // namespace

std::optional<InstallTrustRecord> read_install_trust_record(ConstByteSpan record) {
    if (record.size() < kInstallTrustRecordBytes ||
        std::memcmp(record.data(), kInstallTrustMagic, kInstallTrustMagicBytes) != 0 ||
        crc32(record.data(), kCrcAt) != wire::get_uint32(record.data() + kCrcAt))
        return std::nullopt;
    const size_t label_bytes = record[kLabelBytesAt];
    if (label_bytes > kBrowserLabelBytes) return std::nullopt;
    const ConstByteSpan label(record.data() + kLabelAt, label_bytes);
    const ConstByteSpan padding(record.data() + kLabelAt + label_bytes,
                                kBrowserLabelBytes - label_bytes);
    if (!cbor::valid_utf8(label) || std::memchr(label.data(), 0, label.size()) != nullptr ||
        !zero(padding))
        return std::nullopt;
    InstallTrustRecord out;
    std::memcpy(out.key.data(), record.data() + kKeyAt, out.key.size());
    std::memcpy(out.label, label.data(), label.size());
    return out;
}

std::optional<uint32_t> install_trust_page(uint32_t image_end, uint32_t slot_bytes) {
    const uint64_t page = (uint64_t{image_end} + kInstallTrustPageBytes - 1) /
                          kInstallTrustPageBytes * kInstallTrustPageBytes;
    if (slot_bytes < kInstallTrustPageBytes ||
        page + kInstallTrustPageBytes > slot_bytes - kInstallTrustPageBytes)
        return std::nullopt;
    return static_cast<uint32_t>(page);
}

bool InstallTrust::adopt(ports::Dfu& slots, TrustedBrowsers& browsers) {
    const std::optional<uint32_t> end = running_image_end(slots);
    if (!end) return false;
    const std::optional<uint32_t> page = install_trust_page(*end, slots.running_slot_bytes());
    uint8_t bytes[kInstallTrustRecordBytes];
    if (!page || !slots.read_running_slot(*page, bytes, sizeof(bytes))) return false;
    const std::optional<InstallTrustRecord> record =
        read_install_trust_record(ConstByteSpan(bytes, sizeof(bytes)));
    if (!record) return false;
    browsers.store(record->key, record->label);
    page_ = page;
    landed_ = false;
    return true;
}

bool InstallTrust::erase(ports::Dfu& slots) {
    if (!page_ || !landed_ || !slots.erase_running_slot_page(*page_)) return false;
    page_.reset();
    landed_ = false;
    return true;
}

}  // namespace skyblip::comms
