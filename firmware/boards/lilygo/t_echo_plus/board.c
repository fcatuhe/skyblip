// INFO: fc 20nov26 at board level because MCUboot needs the external flash's rail too: README.md
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/dt-bindings/pinctrl/nrf-pinctrl.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <string.h>

enum {
	// INFO: fc 20nov26 MX25R1635F tVSL, Macronix datasheet rev 1.6, which the ZD25WQ16B
	// alternative undercuts
	FLASH_STARTUP_US = 5000,
	RAIL_RAMP_US = 5000,
};

// INFO: fc 24aug26 these three pins are named as a gpio and as a psel, and both copies are driven
#define BOARD_GPIO_PIN(node_id) \
	(DT_PROP(DT_GPIO_CTLR(node_id, gpios), port) * 32 + DT_GPIO_PIN(node_id, gpios))
#define BOARD_PSEL_PIN(node_id, index) \
	((DT_PROP_BY_IDX(node_id, psels, index) >> NRF_PIN_POS) & NRF_PIN_MSK)

BUILD_ASSERT(BOARD_GPIO_PIN(DT_NODELABEL(epd_sck_gpio)) ==
	     BOARD_PSEL_PIN(DT_CHILD(DT_NODELABEL(spi2_default), group1), 0));
BUILD_ASSERT(BOARD_GPIO_PIN(DT_NODELABEL(epd_mosi_gpio)) ==
	     BOARD_PSEL_PIN(DT_CHILD(DT_NODELABEL(spi2_default), group1), 1));
BUILD_ASSERT(BOARD_GPIO_PIN(DT_NODELABEL(buzzer_sense_gpio)) ==
	     BOARD_PSEL_PIN(DT_CHILD(DT_NODELABEL(pwm0_default), group1), 0));

// INFO: fc 08nov26 the regulator enable, P0.13, feeds this SoC and the radio through U11, so
// nothing lowers it
static const struct gpio_dt_spec board_power_pins[] = {
	GPIO_DT_SPEC_GET(DT_ALIAS(peripheral_rail), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(regulator_enable), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(flash_write_protect), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(flash_hold), gpios),
};

// INFO: fc 20nov26 raised after the rail, reset then wake as SoftRF does, because the receiver
// needs a supply first
static const struct gpio_dt_spec gnss_reset = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_reset), gpios);
static const struct gpio_dt_spec gnss_wake = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_wake), gpios);

static void board_probe_fitted_parts(void);

static int board_power_up_gated_rails(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(board_power_pins); i++) {
		if (!gpio_is_ready_dt(&board_power_pins[i])) {
			return -ENODEV;
		}

		int error = gpio_pin_configure_dt(&board_power_pins[i], GPIO_OUTPUT_ACTIVE);

		if (error != 0) {
			return error;
		}
	}

	k_busy_wait(RAIL_RAMP_US + FLASH_STARTUP_US);

	if (!gpio_is_ready_dt(&gnss_reset) || !gpio_is_ready_dt(&gnss_wake)) {
		return -ENODEV;
	}

	// INFO: fc 20nov26 the reset line is active low in the devicetree, so INACTIVE releases
	// the part
	int error = gpio_pin_configure_dt(&gnss_reset, GPIO_OUTPUT_INACTIVE);

	if (error != 0) {
		return error;
	}

	error = gpio_pin_configure_dt(&gnss_wake, GPIO_OUTPUT_ACTIVE);

	if (error != 0) {
		return error;
	}

	// INFO: fc 20nov26 here, not in its own SYS_INIT, so no priority mistake can probe the
	// panel before its supply
	board_probe_fitted_parts();
	return 0;
}

SYS_INIT(board_power_up_gated_rails, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);

// INFO: fc 20nov26 read at PRE_KERNEL_1, the one moment no driver owns these pins, and skipped
// in MCUboot: README.md
#if !defined(CONFIG_MCUBOOT)

enum {
	// INFO: fc 20nov26 SoftRF's ident timing, nRF52.cpp:3785-3792, whose 200 ms also settles
	// the panel's power-on
	PANEL_RESET_LOW_US = 20000,
	PANEL_SETTLE_US = 200000,
	// INFO: fc 20nov26 half a bit at about 500 kHz, since the panel's controller has no
	// minimum clock
	PANEL_CLOCK_HALF_US = 1,
	// INFO: fc 20nov26 several time constants of the 13 kOhm pull-up into a piezo's tens of
	// nanofarads
	BUZZER_PULLUP_SETTLE_US = 1000,
	PANEL_INTER_REGISTER_US = 1000,
	PANEL_ID_2D_BYTES = 11,
	PANEL_ID_2E_BYTES = 10,
};

#define PANEL_ID_REGISTER_2D 0x2D
#define PANEL_ID_REGISTER_2E 0x2E
#define PANEL_ID_BYTES (PANEL_ID_2D_BYTES + PANEL_ID_2E_BYTES)

static const struct gpio_dt_spec epd_cs =
	GPIO_DT_SPEC_GET_BY_IDX(DT_ALIAS(epd_spi), cs_gpios, 0);
static const struct gpio_dt_spec epd_dc = GPIO_DT_SPEC_GET(DT_NODELABEL(epd_dc_gpio), gpios);
static const struct gpio_dt_spec epd_reset = GPIO_DT_SPEC_GET(DT_NODELABEL(epd_reset_gpio), gpios);
static const struct gpio_dt_spec epd_busy = GPIO_DT_SPEC_GET(DT_NODELABEL(epd_busy_gpio), gpios);
static const struct gpio_dt_spec epd_mosi = GPIO_DT_SPEC_GET(DT_NODELABEL(epd_mosi_gpio), gpios);
static const struct gpio_dt_spec epd_sck = GPIO_DT_SPEC_GET(DT_NODELABEL(epd_sck_gpio), gpios);
static const struct gpio_dt_spec buzzer_sense =
	GPIO_DT_SPEC_GET(DT_NODELABEL(buzzer_sense_gpio), gpios);

static uint8_t panel_id[PANEL_ID_BYTES];
static bool panel_id_read;
static bool buzzer_pin_low_against_pullup;

// INFO: fc 20nov26 mode 0, MSB first, half duplex: MOSI carries the command out and the answer back
static void panel_clock_out(uint8_t value)
{
	for (int bit = 7; bit >= 0; bit--) {
		gpio_pin_set_dt(&epd_mosi, (value >> bit) & 1);
		gpio_pin_set_dt(&epd_sck, 1);
		k_busy_wait(PANEL_CLOCK_HALF_US);
		gpio_pin_set_dt(&epd_sck, 0);
		k_busy_wait(PANEL_CLOCK_HALF_US);
	}
}

static uint8_t panel_clock_in(void)
{
	uint8_t value = 0;

	for (int bit = 7; bit >= 0; bit--) {
		gpio_pin_set_dt(&epd_sck, 1);
		k_busy_wait(PANEL_CLOCK_HALF_US);
		if (gpio_pin_get_dt(&epd_mosi) == 1) {
			value |= (uint8_t)(1u << bit);
		}
		gpio_pin_set_dt(&epd_sck, 0);
		k_busy_wait(PANEL_CLOCK_HALF_US);
	}

	return value;
}

static void panel_read_register(uint8_t register_address, uint8_t *out, size_t bytes)
{
	gpio_pin_configure_dt(&epd_mosi, GPIO_OUTPUT_INACTIVE);
	gpio_pin_set_dt(&epd_dc, 0);
	gpio_pin_set_dt(&epd_cs, 1);

	panel_clock_out(register_address);

	gpio_pin_configure_dt(&epd_mosi, GPIO_INPUT);
	gpio_pin_set_dt(&epd_dc, 1);

	for (size_t i = 0; i < bytes; i++) {
		out[i] = panel_clock_in();
	}

	gpio_pin_set_dt(&epd_sck, 0);
	gpio_pin_set_dt(&epd_dc, 0);
	gpio_pin_set_dt(&epd_cs, 0);
}

static void board_read_panel_id(void)
{
	if (!gpio_is_ready_dt(&epd_cs) || !gpio_is_ready_dt(&epd_dc) ||
	    !gpio_is_ready_dt(&epd_mosi) || !gpio_is_ready_dt(&epd_sck)) {
		return;
	}

	gpio_pin_configure_dt(&epd_cs, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&epd_dc, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&epd_sck, GPIO_OUTPUT_INACTIVE);

	// INFO: fc 20nov26 the devicetree declares this pin active high for io::Gpio, so INACTIVE
	// holds the panel in reset
	gpio_pin_configure_dt(&epd_reset, GPIO_OUTPUT_INACTIVE);
	k_busy_wait(PANEL_RESET_LOW_US);
	gpio_pin_configure_dt(&epd_reset, GPIO_INPUT | GPIO_PULL_UP);
	k_busy_wait(PANEL_SETTLE_US);
	gpio_pin_configure_dt(&epd_busy, GPIO_INPUT);

	panel_read_register(PANEL_ID_REGISTER_2D, panel_id, PANEL_ID_2D_BYTES);
	k_busy_wait(PANEL_INTER_REGISTER_US);
	panel_read_register(PANEL_ID_REGISTER_2E, panel_id + PANEL_ID_2D_BYTES,
			    PANEL_ID_2E_BYTES);

	panel_id_read = true;
}

// INFO: fc 18sep26 SoftRF reads the same pin the same way to tell two boards apart (nRF52.cpp:1265)
static void board_probe_buzzer_pin(void)
{
	if (!gpio_is_ready_dt(&buzzer_sense)) {
		return;
	}

	gpio_pin_configure_dt(&buzzer_sense, GPIO_INPUT);
	const int high_impedance = gpio_pin_get_dt(&buzzer_sense);

	gpio_pin_configure_dt(&buzzer_sense, GPIO_INPUT | GPIO_PULL_UP);
	k_busy_wait(BUZZER_PULLUP_SETTLE_US);
	const int against_pullup = gpio_pin_get_dt(&buzzer_sense);

	gpio_pin_configure_dt(&buzzer_sense, GPIO_INPUT);

	buzzer_pin_low_against_pullup = high_impedance == 0 && against_pullup == 0;
}

static void board_probe_fitted_parts(void)
{
	board_read_panel_id();
	board_probe_buzzer_pin();
}

size_t board_panel_id(uint8_t *out, size_t capacity)
{
	if (!panel_id_read || capacity < PANEL_ID_BYTES) {
		return 0;
	}

	memcpy(out, panel_id, PANEL_ID_BYTES);
	return PANEL_ID_BYTES;
}

int board_buzzer_pin_held_low(void)
{
	return buzzer_pin_low_against_pullup ? 1 : 0;
}

#else

static void board_probe_fitted_parts(void)
{
}

size_t board_panel_id(uint8_t *out, size_t capacity)
{
	(void)out;
	(void)capacity;
	return 0;
}

int board_buzzer_pin_held_low(void)
{
	return 0;
}

#endif /* !CONFIG_MCUBOOT */
