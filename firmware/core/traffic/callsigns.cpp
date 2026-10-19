#include "core/traffic/callsigns.h"

namespace skyblip::traffic {

void CallsignTable::learn(uint8_t address_mapping_table, uint32_t address, const char* callsign,
                          uint32_t now_s) {
    if (callsign == nullptr || callsign[0] == 0) return;
    int index = find_slot(address_mapping_table, address);
    if (index < 0) index = oldest_slot();
    if (index < 0) return;

    Entry& e = slots_[static_cast<size_t>(index)];
    e.used = true;
    e.address = address;
    e.address_mapping_table = address_mapping_table;
    e.heard_s = now_s;
    int n = 0;
    while (n < kTextBytes - 1 && callsign[n] != 0) {
        e.text[n] = callsign[n];
        n++;
    }
    e.text[n] = 0;
}

const char* CallsignTable::find(uint8_t address_mapping_table, uint32_t address) const {
    const int index = find_slot(address_mapping_table, address);
    if (index < 0) return nullptr;
    return slots_[static_cast<size_t>(index)].text;
}

void CallsignTable::age_out(uint32_t now_s, uint32_t forget_s) {
    for (Entry& e : slots_) {
        if (!e.used) continue;
        if (now_s < e.heard_s || now_s - e.heard_s <= forget_s) continue;
        e = Entry{};
    }
}

int CallsignTable::count() const {
    int n = 0;
    for (const Entry& e : slots_)
        if (e.used) n++;
    return n;
}

void CallsignTable::clear() { slots_ = {}; }

int CallsignTable::find_slot(uint8_t address_mapping_table, uint32_t address) const {
    for (int i = 0; i < kMaxCallsigns; i++) {
        const Entry& e = slots_[static_cast<size_t>(i)];
        if (e.used && e.address == address && e.address_mapping_table == address_mapping_table)
            return i;
    }
    return -1;
}

int CallsignTable::oldest_slot() const {
    int victim = -1;
    for (int i = 0; i < kMaxCallsigns; i++) {
        const Entry& e = slots_[static_cast<size_t>(i)];
        if (!e.used) return i;
        if (victim < 0 || e.heard_s < slots_[static_cast<size_t>(victim)].heard_s) victim = i;
    }
    return victim;
}

}  // namespace skyblip::traffic
