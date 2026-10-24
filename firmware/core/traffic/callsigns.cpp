#include "core/traffic/callsigns.h"

namespace skyblip::traffic {

void CallsignTable::learn(uint8_t address_mapping_table, uint32_t address, const char* callsign,
                          uint32_t now_s) {
    if (callsign == nullptr || callsign[0] == 0) return;
    int index = find_slot(address_mapping_table, address);
    if (index < 0) index = oldest_slot();
    if (index < 0) return;

    Entry& entry = slots_[static_cast<size_t>(index)];
    entry.used = true;
    entry.address = address;
    entry.address_mapping_table = address_mapping_table;
    entry.heard_s = now_s;
    int count = 0;
    while (count < kTextBytes - 1 && callsign[count] != 0) {
        entry.text[count] = callsign[count];
        count++;
    }
    entry.text[count] = 0;
}

const char* CallsignTable::find(uint8_t address_mapping_table, uint32_t address) const {
    const int index = find_slot(address_mapping_table, address);
    if (index < 0) return nullptr;
    return slots_[static_cast<size_t>(index)].text;
}

void CallsignTable::age_out(uint32_t now_s, uint32_t forget_s) {
    for (Entry& entry : slots_) {
        if (!entry.used) continue;
        if (now_s < entry.heard_s || now_s - entry.heard_s <= forget_s) continue;
        entry = Entry{};
    }
}

int CallsignTable::count() const {
    int count = 0;
    for (const Entry& entry : slots_)
        if (entry.used) count++;
    return count;
}

void CallsignTable::clear() { slots_ = {}; }

int CallsignTable::find_slot(uint8_t address_mapping_table, uint32_t address) const {
    for (int i = 0; i < kMaxCallsigns; i++) {
        const Entry& entry = slots_[static_cast<size_t>(i)];
        if (entry.used && entry.address == address &&
            entry.address_mapping_table == address_mapping_table)
            return i;
    }
    return -1;
}

int CallsignTable::oldest_slot() const {
    int victim = -1;
    for (int i = 0; i < kMaxCallsigns; i++) {
        const Entry& entry = slots_[static_cast<size_t>(i)];
        if (!entry.used) return i;
        if (victim < 0 || entry.heard_s < slots_[static_cast<size_t>(victim)].heard_s) victim = i;
    }
    return victim;
}

}  // namespace skyblip::traffic
