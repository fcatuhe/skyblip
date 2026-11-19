#include "hardware/platform/zephyr/t_echo_plus.h"
#include "products/common/main_loop.h"
#include "products/skyblip_go/product.h"
#include "products/skyblip_go/services/diagnostics.h"

using namespace skyblip;

namespace {

using Go = go::Product<platform::zephyr::TEchoPlus>;

platform::zephyr::TEchoPlus g_platform;
Go g_product{g_platform};
go::DiagnosticsCollector<Go> g_diagnostics;

}  // namespace

int main(void) { common::run(g_platform, g_product, g_diagnostics); }
