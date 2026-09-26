#include "core/gnss/acquisition.h"

#include "core/gnss/validity.h"

namespace skyblip::gnss {

const char* stage_name(Stage stage) {
    switch (stage) {
        case Stage::Silent: return "SILENT";
        case Stage::Blind: return "BLIND";
        case Stage::Solving: return "SOLVING";
        case Stage::Fixed: return "";
    }
    return "SILENT";
}

Stage stage_of(const GnssSolution& solution) {
    if (solution.fix_valid) return Stage::Fixed;
    if (solution.utc_valid) return Stage::Solving;
    return Stage::Blind;
}

void Acquisition::enter(Stage stage, uint32_t now_ms) {
    if (stage == stage_) return;
    stage_ = stage;
    since_ms_ = now_ms;
}

void Acquisition::observe(const GnssSolution& solution, uint32_t now_ms) {
    enter(stage_of(solution), now_ms);
    solution_ms_ = now_ms;
    heard_ = true;
}

void Acquisition::tick(uint32_t now_ms) {
    if (!heard_) return;
    if (now_ms - solution_ms_ >= kSentenceMaxAgeMs) enter(Stage::Silent, now_ms);
}

}  // namespace skyblip::gnss
