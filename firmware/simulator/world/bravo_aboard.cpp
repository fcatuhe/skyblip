#include "simulator/world/bravo_aboard.h"

namespace skyblip::simulator {

void BravoAboard::load(const Scenario& scenario) {
    scenario_ = scenario;
    next_event_ = 0;
    started_ = false;
    set_fix(true);
    set_link(true);
    follow_own_ship();
}

// INFO: fc 14nov26 the Go's world steps first, so Bravo flies the instant the Go's L76K just flew
void BravoAboard::step(uint32_t now_ms) {
    if (!started_) {
        started_ = true;
        start_ms_ = now_ms;
    }
    bravo_.gnss().tick(now_ms);
    follow_own_ship();
    world_.feel_air(bravo_.barometer());
    apply_events(now_ms);
    world_.squitter_to(bravo_.radio());
    link_.pump();
}

void BravoAboard::set_link(bool up) {
    link_up_ = up;
    link_.set_in_range(up);
    if (!up) link_.drop();
}

// INFO: fc 14nov26 one aircraft: Bravo's AG3335 takes the Go's truth, and keeps its own fix flag
void BravoAboard::follow_own_ship() {
    models::GnssTruth& truth = bravo_.gnss();
    truth = world_.gnss();
}

void BravoAboard::apply_events(uint32_t now_ms) {
    const uint32_t elapsed_ms = now_ms - start_ms_;
    while (next_event_ < scenario_.events.size() &&
           scenario_.events[next_event_].at_ms <= elapsed_ms) {
        const ScenarioEvent& event = scenario_.events[next_event_++];
        const bool on = event.value != 0;
        if (event.kind == EventKind::BravoLink) set_link(on);
        if (!event.on_bravo) continue;
        if (event.kind == EventKind::Fix) set_fix(on);
        if (event.kind == EventKind::Companion && on) connect_companion();
        if (event.kind == EventKind::Companion && !on) disconnect_companion();
    }
}

}  // namespace skyblip::simulator
