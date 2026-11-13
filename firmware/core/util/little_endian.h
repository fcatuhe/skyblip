#ifndef SKYBLIP_CORE_UTIL_LITTLE_ENDIAN_H
#define SKYBLIP_CORE_UTIL_LITTLE_ENDIAN_H

#include "protocols/common/little_endian.h"

namespace skyblip {

using wire::get_int16;
using wire::get_int32;
using wire::get_uint16;
using wire::get_uint24;
using wire::get_uint32;
using wire::put_int16;
using wire::put_int32;
using wire::put_uint16;
using wire::put_uint24;
using wire::put_uint32;

}

#endif
