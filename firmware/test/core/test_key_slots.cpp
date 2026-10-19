// The unit's key in UICR CUSTOMER: four slots written a word at a time, a torn slot never used.
#include <optional>
#include <vector>

#include "core/crypto/ed25519.h"
#include "core/identity/key_slots.h"
#include "doctest/doctest.h"

using skyblip::crypto::Ed25519;
using skyblip::identity::KeySlots;

namespace {

// INFO: fc 18oct26 UICR as the NVMC sees it: bits only fall to 0, and a write can be cut short
struct Uicr : KeySlots::Memory {
    KeySlots::Words words{};
    std::vector<Ed25519::Seed> seeds;
    int writes_before_cut{-1};
    bool read_fails{false};
    bool rng_fails{false};

    Uicr() { words.fill(KeySlots::kErasedWord); }

    bool read(KeySlots::Words& out) const override {
        if (read_fails) return false;
        out = words;
        return true;
    }
    void write(int word, uint32_t value) override {
        if (writes_before_cut == 0) return;
        if (writes_before_cut > 0) writes_before_cut--;
        words[word] &= value;
    }
    bool draw(Ed25519::Seed& out) override {
        if (rng_fails) return false;
        out = seed_numbered(static_cast<uint8_t>(seeds.size() + 1));
        seeds.push_back(out);
        return true;
    }

    static Ed25519::Seed seed_numbered(uint8_t n) {
        Ed25519::Seed seed{};
        for (size_t i = 0; i < seed.size(); i++) seed[i] = static_cast<uint8_t>(n + i);
        return seed;
    }

    void fill_slot(int slot, uint32_t value) {
        for (int i = 0; i < KeySlots::kWordsPerSlot; i++)
            words[slot * KeySlots::kWordsPerSlot + i] = value;
    }
};

}  // namespace

TEST_CASE("key slots: a blank UICR gets a seed in slot 0, read back, and that seed is the key") {
    Uicr uicr;
    CHECK(KeySlots::provision(uicr) == std::optional<int>(0));
    REQUIRE(uicr.seeds.size() == 1);
    CHECK(KeySlots::seed(uicr) == std::optional<Ed25519::Seed>(uicr.seeds[0]));
    CHECK(KeySlots::provision(uicr) == std::optional<int>(0));
    CHECK(uicr.seeds.size() == 1);
}

TEST_CASE("key slots: one erased word in any position makes a slot torn, never a key") {
    for (int word = 0; word < KeySlots::kWordsPerSlot; word++) {
        CAPTURE(word);
        Uicr uicr;
        uicr.fill_slot(0, 0x12345678u);
        uicr.words[word] = KeySlots::kErasedWord;
        CHECK(KeySlots::fill(uicr.words, 0) == KeySlots::Fill::Torn);
        CHECK_FALSE(KeySlots::seed(uicr));
        CHECK(KeySlots::provision(uicr) == std::optional<int>(1));
        CHECK(KeySlots::seed(uicr) == std::optional<Ed25519::Seed>(uicr.seeds.back()));
    }
}

TEST_CASE("key slots: a write cut short leaves a torn slot, and the next blank one takes the key") {
    Uicr uicr;
    uicr.writes_before_cut = 3;
    CHECK_FALSE(KeySlots::provision(uicr));
    CHECK(KeySlots::fill(uicr.words, 0) == KeySlots::Fill::Torn);
    uicr.writes_before_cut = -1;
    CHECK(KeySlots::provision(uicr) == std::optional<int>(1));
    CHECK(KeySlots::seed(uicr) == std::optional<Ed25519::Seed>(uicr.seeds.back()));
}

TEST_CASE("key slots: the key is the last complete slot, and four full slots provision nothing") {
    Uicr uicr;
    for (int slot = 0; slot < KeySlots::kSlots; slot++)
        uicr.fill_slot(slot, 0x01010101u * static_cast<uint32_t>(slot + 1));
    CHECK(KeySlots::current(uicr.words) == std::optional<int>(3));
    CHECK_FALSE(KeySlots::next_blank(uicr.words));
    CHECK(KeySlots::provision(uicr) == std::optional<int>(3));
    CHECK(uicr.seeds.empty());

    uicr.words[KeySlots::kWords - 1] = KeySlots::kErasedWord;
    CHECK(KeySlots::current(uicr.words) == std::optional<int>(2));
    CHECK(KeySlots::provision(uicr) == std::optional<int>(2));
}

TEST_CASE("key slots: a read error hands out no seed and provisions nothing") {
    Uicr uicr;
    uicr.fill_slot(0, 0x12345678u);
    uicr.read_fails = true;
    CHECK_FALSE(KeySlots::seed(uicr));
    CHECK_FALSE(KeySlots::provision(uicr));
    CHECK(uicr.seeds.empty());
}

TEST_CASE("key slots: no seed from the RNG leaves every slot blank") {
    Uicr uicr;
    uicr.rng_fails = true;
    CHECK_FALSE(KeySlots::provision(uicr));
    CHECK(KeySlots::fill(uicr.words, 0) == KeySlots::Fill::Blank);
}
