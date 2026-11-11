#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

enum {
	/* TODO: fc 11nov26 SoftRF waits 30 ms after raising these enables (nRF52.cpp:934-942), unverified until #169 */
	RAIL_SETTLE_US = 30000,
};

static const struct gpio_dt_spec board_rails[] = {
	GPIO_DT_SPEC_GET(DT_ALIAS(flash_enable), gpios),
#if !defined(CONFIG_MCUBOOT)
	GPIO_DT_SPEC_GET(DT_ALIAS(sensor_rail), gpios),
#endif
};

/* INFO: fc 11nov26 PRE_KERNEL_1 after the GPIO driver and before the QSPI NOR driver, which MCUboot needs powered */
static int board_raise_rails(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(board_rails); i++) {
		if (!gpio_is_ready_dt(&board_rails[i])) {
			return -ENODEV;
		}

		int error = gpio_pin_configure_dt(&board_rails[i], GPIO_OUTPUT_ACTIVE);

		if (error != 0) {
			return error;
		}
	}

	k_busy_wait(RAIL_SETTLE_US);
	return 0;
}

SYS_INIT(board_raise_rails, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
