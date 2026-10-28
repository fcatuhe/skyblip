#ifndef SKYBLIP_TEST_SUPPORT_LINK_VECTORS_H
#define SKYBLIP_TEST_SUPPORT_LINK_VECTORS_H

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/util/hex.h"
#include "doctest/doctest.h"

namespace skyblip {

class LinkVectors {
   public:
    static constexpr const char* kPath = "../schemas/link.v1.vectors.json";

    LinkVectors() {
        std::FILE* file = std::fopen(kPath, "rb");
        REQUIRE_MESSAGE(file != nullptr, "cannot open ", kPath);
        char chunk[512];
        size_t count = 0;
        while ((count = std::fread(chunk, 1, sizeof(chunk), file)) > 0) text_.append(chunk, count);
        std::fclose(file);
    }

    std::vector<std::vector<uint8_t>> frames(const char* list) const {
        const size_t opened = text_.find(std::string("\"") + list + "\": [");
        REQUIRE_MESSAGE(opened != std::string::npos, "no list ", list);
        const size_t closed = text_.find("\n  ]", opened);
        std::vector<std::vector<uint8_t>> out;
        const std::string key = "\"hex\": \"";
        for (size_t at = text_.find(key, opened); at < closed; at = text_.find(key, at)) {
            at += key.size();
            const std::string digits = text_.substr(at, text_.find('"', at) - at);
            std::vector<uint8_t> frame(digits.size() / 2);
            REQUIRE(hex::read(digits.c_str(), ByteSpan(frame.data(), frame.size())));
            out.push_back(frame);
        }
        REQUIRE_FALSE(out.empty());
        return out;
    }

   private:
    std::string text_;
};

}  // namespace skyblip

#endif
