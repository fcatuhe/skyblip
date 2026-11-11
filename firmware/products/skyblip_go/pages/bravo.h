#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_BRAVO_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_BRAVO_H

#include <cstdint>

#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pairing.h"

namespace skyblip::go {

struct BravoView {
    bool paired{false};
    uint16_t paired_tag{0};
    BravoLink link{BravoLink::Down};
    BravoStep step{BravoStep::Idle};
    BravoRefusal refusal{BravoRefusal::None};
    uint16_t found_tags[kBravoFoundRows]{};
    int found_count{0};
    uint16_t pairing_tag{0};
};

struct BravoSnapshot {
    BravoView view{};
    int focus{0};
    bool confirming{false};
    bool unpairing{false};
};

void draw_bravo(ui::Canvas& canvas, const BravoSnapshot& snapshot);
void draw_own_icao_notice(ui::Canvas& canvas);
void draw_bravo_forgot_notice(ui::Canvas& canvas);

enum class BravoAction : uint8_t { None, Redraw, Leave, Pair, Cancel, Retry, Unpair };

class BravoPage {
   public:
    void open(uint32_t now_ms);
    BravoAction pad(const BravoView& view, uint32_t now_ms);
    BravoAction button(const BravoView& view, uint32_t now_ms);
    BravoAction idle(const BravoView& view, uint32_t now_ms) const;

    int focus() const { return focus_; }
    bool confirming() const { return confirming_; }
    bool unpairing() const { return unpairing_; }

   private:
    BravoAction pad_paired();
    BravoAction pad_scanning(const BravoView& view);
    BravoAction button_paired();
    BravoAction button_scanning(const BravoView& view);

    uint32_t idle_since_ms_{0};
    int focus_{0};
    bool confirming_{false};
    bool unpairing_{false};
};

}  // namespace skyblip::go

#endif
