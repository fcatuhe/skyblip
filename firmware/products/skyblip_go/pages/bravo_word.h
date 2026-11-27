#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_BRAVO_WORD_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_BRAVO_WORD_H

#include "core/comms/peer.h"
#include "core/gnss/acquisition.h"

namespace skyblip::go {

// INFO: fc 27nov26 the word every page names a Bravo by, nullptr while it brings traffic
inline const char* bravo_word(comms::BravoState state) {
    switch (state) {
        case comms::BravoState::Waiting:
        case comms::BravoState::LinkDown: return "NO LINK";
        case comms::BravoState::ReceiverDown: return "RX DOWN";
        case comms::BravoState::NoUtc:
        case comms::BravoState::WaitingUtc: return gnss::stage_name(gnss::Stage::Blind);
        case comms::BravoState::GpsOff:
        case comms::BravoState::WaitingGpsOff: return gnss::stage_name(gnss::Stage::Silent);
        case comms::BravoState::FindsFix: return gnss::stage_name(gnss::Stage::Solving);
        case comms::BravoState::Live:
        case comms::BravoState::CellLow:
        case comms::BravoState::Unpaired: break;
    }
    return nullptr;
}

}  // namespace skyblip::go

#endif
