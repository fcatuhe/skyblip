#include "boards/seeed/meshtracker_x1/pins.h"
#include "hardware/platform/zephyr/meshtracker_x1.h"
#include "products/common/main_loop.h"
#include "products/skyblip_bravo/product.h"
#include "products/skyblip_bravo/services/diagnostics.h"

using namespace skyblip;

namespace {

using Bravo = bravo::Product<platform::zephyr::MeshtrackerX1>;

platform::zephyr::MeshtrackerX1 g_platform{boards::meshtracker_x1::kFactoryDfuButton};
Bravo g_product{g_platform};
bravo::DiagnosticsCollector<Bravo> g_diagnostics;

}  // namespace

int main(void) { common::run(g_platform, g_product, g_diagnostics); }
