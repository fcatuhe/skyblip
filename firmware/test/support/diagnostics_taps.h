// Harness, not a test: a rig armed, taxied and stopped, and the records its taps left asked after.
#ifndef SKYBLIP_TEST_SUPPORT_DIAGNOSTICS_TAPS_H
#define SKYBLIP_TEST_SUPPORT_DIAGNOSTICS_TAPS_H

#include <cstdint>
#include <vector>

#include "core/diagnostics/payload.h"
#include "core/diagnostics/profile.h"
#include "doctest/doctest.h"
#include "test/support/diagnostics_corpus.h"
#include "test/support/diagnostics_read.h"
#include "test/support/product_rig.h"
#include "test/support/rig_moves.h"

namespace skyblip {

inline int count_of(const std::vector<diagnostics::Record>& records, diagnostics::Type type) {
    int count = 0;
    for (const diagnostics::Record& record : records)
        if (record.type == type) count++;
    return count;
}

template <class T>
inline bool first_of(const std::vector<diagnostics::Record>& records, T& out) {
    for (const diagnostics::Record& record : records)
        if (diagnostics::read(record, out)) return true;
    return false;
}

template <class T>
inline bool last_of(const std::vector<diagnostics::Record>& records, T& out) {
    bool found = false;
    for (const diagnostics::Record& record : records)
        found = diagnostics::read(record, out) || found;
    return found;
}

inline void arm(Rig& rig, diagnostics::Profile profile = diagnostics::Profile::Full) {
    rig.product.diagnostics().arm(profile);
    rig.run_for(100);
    REQUIRE(rig.product.capture().capturing());
}

inline void stop(Rig& rig) {
    rig.product.diagnostics().disarm();
    rig.run_for(4000);
    REQUIRE_FALSE(rig.product.capture().capturing());
}

inline std::vector<diagnostics::Record> armed_taxi(Rig& rig, uint32_t seconds) {
    REQUIRE(rig.setup() == Status::Ok);
    taxi(rig, 3);
    arm(rig);
    taxi(rig, seconds);
    stop(rig);
    return captured(rig);
}

}  // namespace skyblip

#endif
