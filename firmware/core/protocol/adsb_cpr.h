#ifndef SKYBLIP_CORE_PROTOCOL_ADSB_CPR_H
#define SKYBLIP_CORE_PROTOCOL_ADSB_CPR_H

#include <cstdint>

#include "core/protocol/adsb.h"

namespace skyblip::protocol {

enum class CprFormat : uint8_t { Airborne, Surface };

struct CprPosition {
    int32_t latitude_1e7{0};
    int32_t longitude_1e7{0};
};

int cpr_longitude_zones(int32_t latitude_1e7);

bool cpr_global(CprFormat format, const AdsbCpr& newest, const AdsbCpr& other,
                const CprPosition& near, CprPosition& out);

bool cpr_local(CprFormat format, const AdsbCpr& cpr, const CprPosition& reference,
               CprPosition& out);

}

#endif
