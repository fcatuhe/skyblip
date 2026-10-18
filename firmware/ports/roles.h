#ifndef SKYBLIP_PORTS_ROLES_H
#define SKYBLIP_PORTS_ROLES_H

#include "ports/annunciator.h"
#include "ports/capabilities.h"
#include "ports/clock.h"
#include "ports/dfu.h"
#include "ports/die_temperature.h"
#include "ports/display.h"
#include "ports/flash_region.h"
#include "ports/gnss.h"
#include "ports/identity.h"
#include "ports/indicator.h"
#include "ports/kvstore.h"
#include "ports/link.h"
#include "ports/rf.h"

namespace skyblip::ports {

// Every role is a reference: an absent capability is filled by its null part, so
// no caller branches on a pointer. What is absent is stated once, in capabilities.
struct Roles {
    Clock& clock;
    Rf& rf;
    Link& link;
    Display& display;
    KvStore& kv;
    FlashRegion& log_flash;
    Annunciator& annunciator;
    Dfu& dfu;
    DieTemperature& die_temperature;
    Indicator& indicator;
    Gnss& gnss;
    Identity& identity;
    Capabilities capabilities{Capability::None};
};

}  // namespace skyblip::ports

#endif
