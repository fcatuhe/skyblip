#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_ATT_BUDGET_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_ATT_BUDGET_H
#if defined(__ZEPHYR__)

#include <zephyr/sys/atomic.h>

#include "core/comms/link_sessions.h"

namespace skyblip::platform::zephyr {

// INFO: fc 23sep26 a notify off the sysworkq waits forever for a buffer, so each link gets a share
constexpr atomic_val_t kMaxNotifiesInFlight = 4;
constexpr int kAttResponsesPerLink = 1;
// INFO: fc 25sep26 smp_bt.c waits on each notification's sent callback before the next
constexpr int kSmpNotifiesPerLink = 1;
constexpr int kPhoneAttBuffers = kMaxNotifiesInFlight + kAttResponsesPerLink + kSmpNotifiesPerLink;

// INFO: fc 11nov26 the Go's link to Bravo: one peer_claim in flight, one request, one response
constexpr atomic_val_t kCentralWritesInFlight = 1;
constexpr int kCentralRequestsPerLink = 1;
constexpr int kCentralAttBuffers =
    kCentralWritesInFlight + kCentralRequestsPerLink + kAttResponsesPerLink;

static_assert(CONFIG_BT_ATT_TX_COUNT >= kPhoneAttBuffers * CONFIG_BT_MAX_CONN,
              "the ATT pool must hold every phone's notifications, a response and an SMP reply");
#if defined(CONFIG_BT_CENTRAL)
static_assert(CONFIG_BT_ATT_TX_COUNT >=
                  kPhoneAttBuffers * (CONFIG_BT_MAX_CONN - comms::LinkSessions::kPeerConnections) +
                      kCentralAttBuffers,
              "the ATT pool must hold a paired Go's phones and its link to Bravo");
#endif

}  // namespace skyblip::platform::zephyr

#endif  // __ZEPHYR__
#endif
