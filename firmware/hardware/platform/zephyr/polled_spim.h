#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_POLLED_SPIM_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_POLLED_SPIM_H
#if defined(__ZEPHYR__)

#include <nrfx_spim.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>

#include "hardware/io/io.h"

namespace skyblip::platform::zephyr {

struct PolledSpimWiring {
    NRF_SPIM_Type* registers;
    const struct pinctrl_dev_config* pins;
    uint32_t frequency_hz;
    uint8_t rx_delay;
    size_t max_bytes;
    struct gpio_dt_spec cs;
};

// INFO: fc 27nov26 no lock, because one thread drives the bus and never yields inside a transfer
class PolledSpim : public io::Spi {
   public:
    explicit PolledSpim(const PolledSpimWiring& wiring)
        : max_bytes_(wiring.max_bytes), cs_(wiring.cs) {
        spim_.p_reg = wiring.registers;
        ready_ = bring_up(wiring);
    }

    bool ready() const { return ready_; }

    // INFO: fc 27nov26 nrfx with no handler spins on END: hardware/README.md, The X1's radio bus
    bool transfer(const uint8_t* tx, uint8_t* rx, size_t bytes) override {
        if (!ready_ || bytes > max_bytes_) return false;
        const nrfx_spim_xfer_desc_t xfer =
            NRFX_SPIM_XFER_TRX(tx, tx != nullptr ? bytes : 0, rx, rx != nullptr ? bytes : 0);
        return nrfx_spim_xfer(&spim_, &xfer, 0) == 0;
    }
    void select(bool on) override { gpio_pin_set_dt(&cs_, on ? 1 : 0); }

   private:
    // INFO: fc 27nov26 ORC 0x00 clocks zeros for a null tx, as the Go's adapter sends
    bool bring_up(const PolledSpimWiring& wiring) {
        if (gpio_pin_configure_dt(&cs_, GPIO_OUTPUT_INACTIVE) != 0) return false;
        if (pinctrl_apply_state(wiring.pins, PINCTRL_STATE_DEFAULT) != 0) return false;
        nrfx_spim_config_t config =
            NRFX_SPIM_DEFAULT_CONFIG(NRF_SPIM_PIN_NOT_CONNECTED, NRF_SPIM_PIN_NOT_CONNECTED,
                                     NRF_SPIM_PIN_NOT_CONNECTED, NRF_SPIM_PIN_NOT_CONNECTED);
        config.orc = 0x00;
        config.frequency = wiring.frequency_hz;
        config.rx_delay = wiring.rx_delay;
        config.skip_gpio_cfg = true;
        config.skip_psel_cfg = true;
        return nrfx_spim_init(&spim_, &config, nullptr, nullptr) == 0;
    }

    nrfx_spim_t spim_{};
    size_t max_bytes_;
    struct gpio_dt_spec cs_;
    bool ready_{false};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
