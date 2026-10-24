// Harness, not a test: schemas/activation.v1.vectors.json, which the server checks too.
#ifndef SKYBLIP_TEST_SUPPORT_ACTIVATION_VECTORS_H
#define SKYBLIP_TEST_SUPPORT_ACTIVATION_VECTORS_H

#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "core/util/hex.h"
#include "doctest/doctest.h"
#include "simulator/world/json.h"

namespace skyblip {

class ActivationVectors {
   public:
    // INFO: fc 18oct26 make runs the suite from firmware/, which CI and make -C both do
    static constexpr const char* kPath = "../schemas/activation.v1.vectors.json";

    ActivationVectors() {
        std::FILE* file = std::fopen(kPath, "rb");
        REQUIRE_MESSAGE(file != nullptr, "cannot open ", kPath);
        char chunk[512];
        size_t count = 0;
        while ((count = std::fread(chunk, 1, sizeof(chunk), file)) > 0) text_.append(chunk, count);
        std::fclose(file);
    }

    std::string text(const char* key) const {
        char value[512] = {0};
        REQUIRE_MESSAGE(reader().read_string(key, value, sizeof(value)), "no key ", key);
        return value;
    }

    bool flag(const char* key) const {
        bool value = false;
        REQUIRE_MESSAGE(reader().read_boolean(key, value), "no key ", key);
        return value;
    }

    long number(const char* key) const {
        long value = 0;
        REQUIRE_MESSAGE(reader().read_integer(key, value), "no key ", key);
        return value;
    }

    template <size_t N>
    std::array<uint8_t, N> bytes(const char* key) const {
        std::array<uint8_t, N> out{};
        REQUIRE(hex::read(text(key).c_str(), ByteSpan(out.data(), N)));
        return out;
    }

    std::vector<uint8_t> message(const char* key) const {
        const std::string hex_text = text(key);
        std::vector<uint8_t> out(hex_text.size() / 2);
        REQUIRE(hex::read(hex_text.c_str(), ByteSpan(out.data(), out.size())));
        return out;
    }

   private:
    simulator::json::Reader reader() const {
        return simulator::json::Reader(text_.data(), static_cast<int>(text_.size()));
    }

    std::string text_;
};

}  // namespace skyblip

#endif
