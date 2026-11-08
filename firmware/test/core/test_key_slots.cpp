// The unit's key in UICR CUSTOMER: four slots written a word at a time, a torn slot never used.
#include <deque>
#include <optional>
#include <vector>

#include "core/crypto/ed25519.h"
#include "core/identity/key_slots.h"
#include "core/util/hex.h"
#include "doctest/doctest.h"

using skyblip::ByteSpan;
using skyblip::crypto::Ed25519;
using skyblip::identity::KeySlots;

namespace {

// INFO: fc 18oct26 UICR as the NVMC sees it: bits only fall to 0, and a write can be cut short
struct Uicr : KeySlots::Memory {
    KeySlots::Words words{};
    std::vector<KeySlots::Material> drawn;
    std::deque<KeySlots::Material> scripted;
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
    bool draw(KeySlots::Material& out) override {
        if (rng_fails) return false;
        if (scripted.empty()) {
            out = material_numbered(static_cast<uint8_t>(drawn.size() + 1));
        } else {
            out = scripted.front();
            scripted.pop_front();
        }
        drawn.push_back(out);
        return true;
    }

    static KeySlots::Material material_numbered(uint8_t number) {
        KeySlots::Material material{};
        for (size_t i = 0; i < material.size(); i++) material[i] = static_cast<uint8_t>(number + i);
        return material;
    }

    void fill_slot(int slot, uint32_t value) {
        for (int i = 0; i < KeySlots::kWordsPerSlot; i++)
            words[slot * KeySlots::kWordsPerSlot + i] = value;
    }

    void put(int slot, const KeySlots::Material& material) {
        const KeySlots::Slot written = KeySlots::slot_of(material);
        for (int i = 0; i < KeySlots::kWordsPerSlot; i++)
            words[slot * KeySlots::kWordsPerSlot + i] = written[i];
    }
};

std::optional<Ed25519::Seed> seed_of(const KeySlots::Material& material) {
    return KeySlots::seed_of(material);
}

}  // namespace

// Python's hashlib: sha256 of "skyblip:slot:v1" or "skyblip:seed:v1" then bytes 0 to 27.
TEST_CASE("key slots: a slot is the material then its tag, and the seed is its labelled SHA-256") {
    KeySlots::Material material{};
    for (size_t i = 0; i < material.size(); i++) material[i] = static_cast<uint8_t>(i);
    const KeySlots::Slot slot = KeySlots::slot_of(material);
    CHECK(slot[0] == 0x03020100u);
    CHECK(slot[6] == 0x1B1A1918u);
    CHECK(slot[7] == 0x86179A09u);
    Ed25519::Seed expected{};
    REQUIRE(skyblip::hex::read("e22c2ad4e30fcb3926ba011b80023e798074e1d681efb3e1799488d1c0a86edb",
                               ByteSpan(expected.data(), expected.size())));
    CHECK(KeySlots::seed_of(material) == expected);
}

TEST_CASE("key slots: a blank UICR gets material in slot 0, read back, and its seed is the key") {
    Uicr uicr;
    CHECK(KeySlots::provision(uicr) == std::optional<int>(0));
    REQUIRE(uicr.drawn.size() == 1);
    CHECK(KeySlots::fill(uicr.words, 0) == KeySlots::Fill::Complete);
    CHECK(KeySlots::seed(uicr) == seed_of(uicr.drawn[0]));
    CHECK(KeySlots::provision(uicr) == std::optional<int>(0));
    CHECK(uicr.drawn.size() == 1);
}

TEST_CASE("key slots: one erased word in any position makes a slot torn, never a key") {
    for (int word = 0; word < KeySlots::kWordsPerSlot; word++) {
        CAPTURE(word);
        Uicr uicr;
        uicr.put(0, Uicr::material_numbered(0x40));
        uicr.words[word] = KeySlots::kErasedWord;
        CHECK(KeySlots::fill(uicr.words, 0) == KeySlots::Fill::Torn);
        CHECK_FALSE(KeySlots::seed(uicr));
        CHECK(KeySlots::provision(uicr) == std::optional<int>(1));
        CHECK(KeySlots::seed(uicr) == seed_of(uicr.drawn.back()));
    }
}

// Another firmware, or a factory, may write CUSTOMER: a slot it filled has no tag of ours.
TEST_CASE("key slots: a slot full of foreign data is torn, and the next blank one takes the key") {
    for (const uint32_t foreign : {0x00000000u, 0x12345678u, 0xFFFF0000u}) {
        CAPTURE(foreign);
        Uicr uicr;
        uicr.fill_slot(0, foreign);
        CHECK(KeySlots::fill(uicr.words, 0) == KeySlots::Fill::Torn);
        CHECK_FALSE(KeySlots::seed(uicr));
        CHECK(KeySlots::provision(uicr) == std::optional<int>(1));
        CHECK(KeySlots::seed(uicr) == seed_of(uicr.drawn.back()));
    }
}

// A brown-out inside a word's write can leave it half programmed: some of its bits fallen, not all.
TEST_CASE("key slots: a word left half programmed in any position makes a slot torn") {
    for (int word = 0; word < KeySlots::kWordsPerSlot; word++) {
        CAPTURE(word);
        Uicr uicr;
        uicr.put(0, Uicr::material_numbered(0x40));
        const KeySlots::Slot whole = KeySlots::slot_of(Uicr::material_numbered(0x40));
        const uint32_t zeros = ~whole[word];
        uicr.words[word] = whole[word] | (zeros & (~zeros + 1u));
        REQUIRE(uicr.words[word] != whole[word]);
        CHECK(KeySlots::fill(uicr.words, 0) == KeySlots::Fill::Torn);
        CHECK_FALSE(KeySlots::seed(uicr));
    }
}

TEST_CASE("key slots: a write cut short leaves a torn slot, and the next blank one takes the key") {
    Uicr uicr;
    uicr.writes_before_cut = 3;
    CHECK_FALSE(KeySlots::provision(uicr));
    CHECK(KeySlots::fill(uicr.words, 0) == KeySlots::Fill::Torn);
    uicr.writes_before_cut = -1;
    CHECK(KeySlots::provision(uicr) == std::optional<int>(1));
    CHECK(KeySlots::seed(uicr) == seed_of(uicr.drawn.back()));
}

TEST_CASE("key slots: the key is the last complete slot, and four full slots provision nothing") {
    Uicr uicr;
    for (int slot = 0; slot < KeySlots::kSlots; slot++)
        uicr.put(slot, Uicr::material_numbered(static_cast<uint8_t>(0x40 + slot)));
    CHECK(KeySlots::current(uicr.words) == std::optional<int>(3));
    CHECK_FALSE(KeySlots::next_blank(uicr.words));
    CHECK(KeySlots::provision(uicr) == std::optional<int>(3));
    CHECK(KeySlots::seed(uicr) == seed_of(Uicr::material_numbered(0x43)));
    CHECK(uicr.drawn.empty());

    uicr.words[KeySlots::kWords - 1] = KeySlots::kErasedWord;
    CHECK(KeySlots::current(uicr.words) == std::optional<int>(2));
    CHECK(KeySlots::provision(uicr) == std::optional<int>(2));
}

TEST_CASE("key slots: four slots of foreign data leave the unit keyless and draw nothing") {
    Uicr uicr;
    for (int slot = 0; slot < KeySlots::kSlots; slot++) uicr.fill_slot(slot, 0);
    CHECK_FALSE(KeySlots::provision(uicr));
    CHECK_FALSE(KeySlots::seed(uicr));
    CHECK(uicr.drawn.empty());
}

TEST_CASE("key slots: a read error hands out no seed and provisions nothing") {
    Uicr uicr;
    uicr.put(0, Uicr::material_numbered(0x40));
    uicr.read_fails = true;
    CHECK_FALSE(KeySlots::seed(uicr));
    CHECK_FALSE(KeySlots::provision(uicr));
    CHECK(uicr.drawn.empty());
}

TEST_CASE("key slots: no material from the RNG leaves every slot blank") {
    Uicr uicr;
    uicr.rng_fails = true;
    CHECK_FALSE(KeySlots::provision(uicr));
    CHECK(KeySlots::fill(uicr.words, 0) == KeySlots::Fill::Blank);
}

TEST_CASE("key slots: a draw holding an erased word is drawn again, and the next one is stored") {
    Uicr uicr;
    KeySlots::Material erased_word = Uicr::material_numbered(0x40);
    for (size_t i = 8; i < 12; i++) erased_word[i] = 0xFF;
    uicr.scripted.push_back(erased_word);
    CHECK_FALSE(KeySlots::storable(erased_word));
    CHECK(KeySlots::provision(uicr) == std::optional<int>(0));
    REQUIRE(uicr.drawn.size() == 2);
    CHECK(KeySlots::seed(uicr) == seed_of(uicr.drawn[1]));
}

// An RNG that fails silently hands back the buffer as it was, or one byte over and over.
TEST_CASE("key slots: an RNG that repeats one byte is never a key, and the unit stays keyless") {
    for (const uint8_t stuck : {0x00, 0x5A}) {
        CAPTURE(stuck);
        Uicr uicr;
        KeySlots::Material repeated{};
        repeated.fill(stuck);
        CHECK_FALSE(KeySlots::storable(repeated));
        for (int draw = 0; draw < 4; draw++) uicr.scripted.push_back(repeated);
        CHECK_FALSE(KeySlots::provision(uicr));
        CHECK(KeySlots::fill(uicr.words, 0) == KeySlots::Fill::Blank);
        CHECK(uicr.drawn.size() == 4);
    }
}
