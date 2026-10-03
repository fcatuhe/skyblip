#include "hardware/parts/bhi260/image_store.h"

#include <cstring>

#include "core/store/sector.h"

namespace skyblip::parts {

namespace {

void put_u16(uint8_t* out, uint16_t v) {
    out[0] = static_cast<uint8_t>(v);
    out[1] = static_cast<uint8_t>(v >> 8);
}

void put_u32(uint8_t* out, uint32_t v) {
    put_u16(out, static_cast<uint16_t>(v));
    put_u16(out + 2, static_cast<uint16_t>(v >> 16));
}

uint16_t get_u16(const uint8_t* in) { return static_cast<uint16_t>(in[0] | (in[1] << 8)); }

uint32_t get_u32(const uint8_t* in) {
    return static_cast<uint32_t>(get_u16(in)) | (static_cast<uint32_t>(get_u16(in + 2)) << 16);
}

uint32_t at_most(uint32_t a, uint32_t b) { return a < b ? a : b; }

}  // namespace

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
    if (header.magic != kMagic || header.layout != kLayout || header.length == 0 ||
        header.length > capacity()) {
        holding_ = Holding::Corrupt;
        return;
    }
    holding_ = verify_payload(header.length, header.digest);
    if (holding_ != Holding::Held) return;
    length_ = header.length;
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
    length_ = static_cast<uint32_t>(image.size());
    sectors_ = (kHeaderBytes + length_ + flash_.sector_bytes() - 1) / flash_.sector_bytes();
    next_sector_ = 0;
    done_ = 0;
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

bool Bhi260ImageStore::read(uint32_t offset, uint8_t* out, uint16_t len) {
    if (holding_ != Holding::Held) return false;
    if (offset > length_ || len > length_ - offset) return false;
    return is_ok(flash_.read(kHeaderBytes + offset, out, len));
}

void Bhi260ImageStore::encode(const Header& header, uint8_t* out) {
    put_u32(out, header.magic);
    put_u16(out + 4, header.layout);
    put_u16(out + 6, 0);
    put_u32(out + 8, header.length);
    std::memcpy(out + 12, header.digest.data(), Sha256::kDigestBytes);
}

Bhi260ImageStore::Header Bhi260ImageStore::decode(const uint8_t* raw) {
    Header header;
    header.magic = get_u32(raw);
    header.layout = get_u16(raw + 4);
    header.length = get_u32(raw + 8);
    std::memcpy(header.digest.data(), raw + 12, Sha256::kDigestBytes);
    return header;
}

Bhi260ImageStore::Holding Bhi260ImageStore::verify_payload(uint32_t length,
                                                           const Sha256::Digest& expected) {
    Sha256 sha;
    for (uint32_t at = 0; at < length; at += kVerifyChunkBytes) {
        const uint32_t take = at_most(kVerifyChunkBytes, length - at);
        if (!is_ok(flash_.read(kHeaderBytes + at, chunk_, take))) return Holding::Unreadable;
        sha.update(chunk_, take);
    }
    return sha.finish() == expected ? Holding::Held : Holding::Corrupt;
}

void Bhi260ImageStore::step_erase() {
    if (!is_ok(flash_.erase_sector(next_sector_))) {
        stop(Holding::Unreadable);
        return;
    }
    next_sector_++;
    if (next_sector_ < sectors_) return;
    phase_ = Phase::Program;
    done_ = 0;
}

void Bhi260ImageStore::step_program() {
    const uint32_t take = at_most(store::kPageBytes, length_ - done_);
    if (!is_ok(flash_.write(kHeaderBytes + done_, source_.data() + done_, take))) {
        stop(Holding::Unreadable);
        return;
    }
    done_ += take;
    if (done_ < length_) return;
    phase_ = Phase::Commit;
}

void Bhi260ImageStore::step_commit() {
    Header header;
    header.magic = kMagic;
    header.layout = kLayout;
    header.length = length_;
    header.digest = digest_;
    uint8_t raw[kHeaderUsedBytes];
    encode(header, raw);
    if (!is_ok(flash_.write(0, raw, sizeof(raw)))) {
        stop(Holding::Unreadable);
        return;
    }
    phase_ = Phase::Verify;
    done_ = 0;
    verifying_.reset();
}

void Bhi260ImageStore::step_verify() {
    const uint32_t take = at_most(kVerifyChunkBytes, length_ - done_);
    if (!is_ok(flash_.read(kHeaderBytes + done_, chunk_, take))) {
        stop(Holding::Unreadable);
        return;
    }
    verifying_.update(chunk_, take);
    done_ += take;
    if (done_ < length_) return;
    stop(verifying_.finish() == digest_ ? Holding::Held : Holding::Corrupt);
}

void Bhi260ImageStore::stop(Holding why) {
    holding_ = why;
    source_ = ConstByteSpan{};
}

}  // namespace skyblip::parts
