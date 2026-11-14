#include "protocols/adsb/registration.h"

#include <cstring>

namespace skyblip::adsb {

namespace {

struct NationalBlock {
    uint32_t first;
    uint32_t last;
    const char* prefix;
};

// INFO: fc 13nov26 ICAO Annex 10 Vol III Part I Ch 9 blocks, each with its nationality mark
constexpr NationalBlock kBlocks[] = {
    {0x300000, 0x33FFFF, "I"},  {0x340000, 0x37FFFF, "EC"}, {0x380000, 0x3BFFFF, "F"},
    {0x3C0000, 0x3FFFFF, "D"},  {0x400000, 0x43FFFF, "G"},  {0x440000, 0x447FFF, "OE"},
    {0x448000, 0x44FFFF, "OO"}, {0x458000, 0x45FFFF, "OY"}, {0x460000, 0x467FFF, "OH"},
    {0x470000, 0x477FFF, "HA"}, {0x478000, 0x47FFFF, "LN"}, {0x480000, 0x487FFF, "PH"},
    {0x488000, 0x48FFFF, "SP"}, {0x490000, 0x497FFF, "CS"}, {0x498000, 0x49FFFF, "OK"},
    {0x4A8000, 0x4AFFFF, "SE"}, {0x4B0000, 0x4B7FFF, "HB"}, {0x4CA000, 0x4CAFFF, "EI"},
};

constexpr size_t kMarkFloorCharacters = 3;
constexpr size_t kMarkCeilingCharacters = 5;

const char* nationality_of(uint32_t address) {
    for (const NationalBlock& block : kBlocks)
        if (address >= block.first && address <= block.last) return block.prefix;
    return nullptr;
}

bool letter(char character) { return character >= 'A' && character <= 'Z'; }
bool digit(char character) { return character >= '0' && character <= '9'; }

// INFO: fc 13nov26 a mark is all letters or all digits, so an airline's DLH4AB stays as heard
bool registration_mark(const char* mark) {
    const size_t length = std::strlen(mark);
    if (length < kMarkFloorCharacters || length > kMarkCeilingCharacters) return false;
    bool letters = true, digits = true;
    for (size_t i = 0; i < length; i++) {
        letters = letters && letter(mark[i]);
        digits = digits && digit(mark[i]);
    }
    return letters || digits;
}

int copied(char* out, size_t capacity, const char* text) {
    size_t length = std::strlen(text);
    if (length >= capacity) length = capacity - 1;
    std::memcpy(out, text, length);
    out[length] = 0;
    return static_cast<int>(length);
}

}  // namespace

int registration(char* out, size_t capacity, const char* callsign, uint32_t address) {
    if (capacity == 0) return 0;
    const char* prefix = nationality_of(address);
    const size_t prefix_length = prefix == nullptr ? 0 : std::strlen(prefix);
    if (prefix == nullptr || std::strncmp(callsign, prefix, prefix_length) != 0 ||
        !registration_mark(callsign + prefix_length) || std::strlen(callsign) + 1 >= capacity)
        return copied(out, capacity, callsign);
    int written = copied(out, capacity, prefix);
    out[written++] = '-';
    return written +
           copied(out + written, capacity - static_cast<size_t>(written), callsign + prefix_length);
}

}
