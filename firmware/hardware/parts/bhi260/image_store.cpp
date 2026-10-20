#include "hardware/parts/bhi260/image_store.h"

#include <algorithm>
#include <cstring>

#include "core/store/sector.h"
#include "core/util/little_endian.h"

namespace skyblip::parts {

void Bhi260ImageStore::check() {
    if (!flash_.ready()) {
        holding_ = Holding::Unreadable;
        return;
    }
    uint8_t raw[kHeaderUsedBytes];
    if (!is_ok(flash_.read(0, raw, sizeof(raw)))) {
        holding_ = Holding::Unreadable;
        return;
    }
    if (store::erased(raw, sizeof(raw))) {
        holding_ = Holding::Blank;
        return;
    }
    const Header header = decode(raw);
    if (header.magic != kMagic || header.layout_version != kLayoutVersion ||
        header.image_bytes == 0 || header.image_bytes > capacity()) {
        holding_ = Holding::Corrupt;
        return;
    }
    holding_ = verify_payload(header.image_bytes, header.digest);
    if (holding_ != Holding::Held) return;
    image_bytes_ = header.image_bytes;
    digest_ = header.digest;
}

uint32_t Bhi260ImageStore::capacity() const {
    const uint32_t region = flash_.sector_bytes() * flash_.sector_count();
    return region > kHeaderBytes ? region - kHeaderBytes : 0;
}

bool Bhi260ImageStore::begin_write(ConstByteSpan image, const Sha256::Digest& digest) {
    if (!flash_.ready()) {
        holding_ = Holding::Unreadable;
        return false;
    }
    if (image.empty() || image.size() > capacity() || flash_.sector_bytes() == 0) return false;
    source_ = image;
    digest_ = digest;
    image_bytes_ = static_cast<uint32_t>(image.size());
    sector_count_ =
        (kHeaderBytes + image_bytes_ + flash_.sector_bytes() - 1) / flash_.sector_bytes();
    next_sector_ = 0;
    done_bytes_ = 0;
    phase_ = Phase::Erase;
    holding_ = Holding::Writing;
    return true;
}

uint32_t Bhi260ImageStore::next_cost_ms() const {
    switch (phase_) {
        case Phase::Erase: return store::kSectorEraseCostMs;
        case Phase::Program:
        case Phase::Commit: return store::kPageWriteCostMs;
        case Phase::Verify: return 0;
    }
    return store::kSectorEraseCostMs;
}

void Bhi260ImageStore::step() {
    if (holding_ != Holding::Writing) return;
    switch (phase_) {
        case Phase::Erase: step_erase(); return;
        case Phase::Program: step_program(); return;
        case Phase::Commit: step_commit(); return;
        case Phase::Verify: step_verify(); return;
    }
}

bool Bhi260ImageStore::read(uint32_t offset, uint8_t* out, uint16_t bytes) {
    if (holding_ != Holding::Held) return false;
    if (offset > image_bytes_ || bytes > image_bytes_ - offset) return false;
    return is_ok(flash_.read(kHeaderBytes + offset, out, bytes));
}

void Bhi260ImageStore::encode(const Header& header, uint8_t* out) {
    put_uint32(out, header.magic);
    put_uint16(out + 4, header.layout_version);
    put_uint16(out + 6, 0);
    put_uint32(out + 8, header.image_bytes);
    std::memcpy(out + 12, header.digest.data(), Sha256::kDigestBytes);
}

Bhi260ImageStore::Header Bhi260ImageStore::decode(const uint8_t* raw) {
    Header header;
    header.magic = get_uint32(raw);
    header.layout_version = get_uint16(raw + 4);
    header.image_bytes = get_uint32(raw + 8);
    std::memcpy(header.digest.data(), raw + 12, Sha256::kDigestBytes);
    return header;
}

Bhi260ImageStore::Holding Bhi260ImageStore::verify_payload(uint32_t bytes,
                                                           const Sha256::Digest& expected) {
    Sha256 hash;
    for (uint32_t at = 0; at < bytes; at += kVerifyChunkBytes) {
        const uint32_t take = std::min(kVerifyChunkBytes, bytes - at);
        if (!is_ok(flash_.read(kHeaderBytes + at, chunk_, take))) return Holding::Unreadable;
        hash.update(chunk_, take);
    }
    return hash.finish() == expected ? Holding::Held : Holding::Corrupt;
}

void Bhi260ImageStore::step_erase() {
    if (!is_ok(flash_.erase_sector(next_sector_))) {
        stop(Holding::Unreadable);
        return;
    }
    next_sector_++;
    if (next_sector_ < sector_count_) return;
    phase_ = Phase::Program;
    done_bytes_ = 0;
}

void Bhi260ImageStore::step_program() {
    const uint32_t take = std::min(store::kPageBytes, image_bytes_ - done_bytes_);
    if (!is_ok(flash_.write(kHeaderBytes + done_bytes_, source_.data() + done_bytes_, take))) {
        stop(Holding::Unreadable);
        return;
    }
    done_bytes_ += take;
    if (done_bytes_ < image_bytes_) return;
    phase_ = Phase::Commit;
}

void Bhi260ImageStore::step_commit() {
    Header header;
    header.magic = kMagic;
    header.layout_version = kLayoutVersion;
    header.image_bytes = image_bytes_;
    header.digest = digest_;
    uint8_t raw[kHeaderUsedBytes];
    encode(header, raw);
    if (!is_ok(flash_.write(0, raw, sizeof(raw)))) {
        stop(Holding::Unreadable);
        return;
    }
    phase_ = Phase::Verify;
    done_bytes_ = 0;
    verifying_.reset();
}

void Bhi260ImageStore::step_verify() {
    const uint32_t take = std::min(kVerifyChunkBytes, image_bytes_ - done_bytes_);
    if (!is_ok(flash_.read(kHeaderBytes + done_bytes_, chunk_, take))) {
        stop(Holding::Unreadable);
        return;
    }
    verifying_.update(chunk_, take);
    done_bytes_ += take;
    if (done_bytes_ < image_bytes_) return;
    stop(verifying_.finish() == digest_ ? Holding::Held : Holding::Corrupt);
}

void Bhi260ImageStore::stop(Holding why) {
    holding_ = why;
    source_ = ConstByteSpan{};
}

}  // namespace skyblip::parts
