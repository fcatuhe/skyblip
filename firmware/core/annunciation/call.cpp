#include "core/annunciation/call.h"

namespace skyblip::annunciation {

Follower::Heard Follower::follow(const Call& call) {
    Heard heard{};
    heard.announced = heard_ && *heard_ != call.announcement;
    heard_ = call.announcement;
    if (dismissed_ == call.announcement) return Heard{};
    heard.level = call.announcing;
    heard.undismissed = call.undismissed;
    return heard;
}

void Follower::dismiss() {
    if (heard_) dismissed_ = heard_;
}

void Follower::lose() {
    heard_.reset();
    dismissed_.reset();
}

}  // namespace skyblip::annunciation
