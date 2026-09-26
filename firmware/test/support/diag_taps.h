// Harness, not a test: a rig armed, taxied and stopped, and the records its taps left asked after.
#ifndef SKYBLIP_TEST_SUPPORT_DIAG_TAPS_H
#define SKYBLIP_TEST_SUPPORT_DIAG_TAPS_H

#include <cstdint>
#include <vector>

#include "core/diag/payload.h"
#include "core/diag/profile.h"
#include "doctest/doctest.h"
#include "test/support/diag_corpus.h"
#include "test/support/product_rig.h"
#include "test/support/rig_moves.h"

namespace skyblip {

inline int count_of(const std::vector<diag::Record>& records, diag::Type type) {
    int n = 0;
    for (const diag::Record& record : records)
        if (record.type == type) n++;
    return n;
}

template <class T>
inline bool first_of(const std::vector<diag::Record>& records, T& out) {
    for (const diag::Record& record : records)
        if (diag::read(record, out)) return true;
    return false;
}

template <class T>
inline bool last_of(const std::vector<diag::Record>& records, T& out) {
    bool found = false;
    for (const diag::Record& record : records) found = diag::read(record, out) || found;
    return found;
}

inline void arm(Rig& rig, uint32_t& t, diag::Profile profile = diag::Profile::Full) {
    rig.product.diag().arm(profile);
    rig.run(t, t + 100);
    t += 100;
    REQUIRE(rig.product.capture().capturing());
}

inline void stop(Rig& rig, uint32_t& t) {
    rig.product.diag().disarm();
    rig.run(t, t + 4000);
    t += 4000;
    REQUIRE_FALSE(rig.product.capture().capturing());
}

inline std::vector<diag::Record> armed_taxi(Rig& rig, uint32_t& t, uint32_t seconds) {
    REQUIRE(rig.setup() == Status::Ok);
    taxi(rig, t, 3);
    arm(rig, t);
    taxi(rig, t, seconds);
    stop(rig, t);
    return captured(rig);
}

}  // namespace skyblip

#endif
