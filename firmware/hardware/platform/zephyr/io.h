#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_IO_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_IO_H
#if defined(__ZEPHYR__)

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#include "core/util/result.h"
#include "hardware/io/io.h"
#include "hardware/platform/zephyr/loop_wake.h"

namespace skyblip::platform::zephyr {

class Gpio : public io::Gpio {
   public:
    Gpio(const struct device* gpio0, const struct device* gpio1) : port_{gpio0, gpio1} {}

    void set(int pin, bool level) override { gpio_pin_set(port_of(pin), bit(pin), level ? 1 : 0); }
    bool get(int pin) override { return gpio_pin_get(port_of(pin), bit(pin)) == 1; }
    void mode_output(int pin) override { gpio_pin_configure(port_of(pin), bit(pin), GPIO_OUTPUT); }
    void mode_input(int pin, bool pullup) override {
        gpio_pin_configure(port_of(pin), bit(pin), GPIO_INPUT | (pullup ? GPIO_PULL_UP : 0));
    }

   private:
    const struct device* port_of(int pin) const { return port_[(pin >> 5) & 1]; }
    static gpio_pin_t bit(int pin) { return static_cast<gpio_pin_t>(pin & 31); }
    const struct device* port_[2];
};

class Delay : public io::Delay {
   public:
    void wait_at_least_us(uint32_t hold_us) override {
        if (!may_sleep_through(hold_us)) {
            k_busy_wait(hold_us);
            return;
        }
        // INFO: fc 26sep26 k_usleep returns early, with the time still owed, if the thread is woken
        int32_t owed_us = static_cast<int32_t>(hold_us);
        while (owed_us > 0) owed_us = k_usleep(owed_us);
    }

   private:
    static constexpr uint32_t kTickUs =
        (1000000u + CONFIG_SYS_CLOCK_TICKS_PER_SEC - 1) / CONFIG_SYS_CLOCK_TICKS_PER_SEC;

    static bool may_sleep_through(uint32_t hold_us) { return hold_us >= kTickUs && k_can_yield(); }
};

// INFO: fc 20nov26 the drivers drive CS through select(), so CS stays out of spi_config
class Spi : public io::Spi {
   public:
    Spi(const struct device* spi, const struct spi_config& config, const struct gpio_dt_spec& cs)
        : spi_(spi), config_(config), cs_(cs) {
        gpio_pin_configure_dt(&cs_, GPIO_OUTPUT_INACTIVE);
    }

    void transfer(const uint8_t* tx, uint8_t* rx, size_t bytes) override {
        static uint8_t zeros[64] = {0};
        const uint8_t* transmitted = tx ? tx : zeros;
        struct spi_buf tx_buffer{const_cast<uint8_t*>(transmitted), bytes};
        struct spi_buf rx_buffer{rx, bytes};
        struct spi_buf_set tx_buffers{&tx_buffer, 1};
        struct spi_buf_set rx_buffers{&rx_buffer, 1};
        spi_transceive(spi_, &config_, &tx_buffers, rx ? &rx_buffers : nullptr);
    }
    void select(bool on) override { gpio_pin_set_dt(&cs_, on ? 1 : 0); }

   private:
    const struct device* spi_;
    struct spi_config config_;
    struct gpio_dt_spec cs_;
};

// INFO: fc 20nov26 Zephyr's barometer driver shares this bus, and i2c_transfer takes its lock
class I2c : public io::I2c {
   public:
    explicit I2c(const struct device* bus) : bus_(bus) {}

    bool write(uint8_t address, const uint8_t* data, size_t bytes) override {
        if (!device_is_ready(bus_)) return false;
        // TODO: fc 20nov26 confirm on silicon that nRF TWIM sends a zero-length probe
        if (bytes == 0) {
            uint8_t nothing = 0;
            struct i2c_msg probe{&nothing, 0, I2C_MSG_WRITE | I2C_MSG_STOP};
            return i2c_transfer(bus_, &probe, 1, address) == 0;
        }
        return i2c_write(bus_, data, bytes, address) == 0;
    }

    bool read(uint8_t address, uint8_t* out, size_t bytes) override {
        if (!device_is_ready(bus_)) return false;
        return i2c_read(bus_, out, bytes, address) == 0;
    }

   private:
    const struct device* bus_;
};

// INFO: fc 04sep26 polled UARTE receive holds 1 byte + a 4-byte FIFO, a 10 ms loop drops NMEA
class Uart : public io::Uart, public io::UartRate {
   public:
    // INFO: fc 28sep26 two seconds of GSV at 115200: 512 B overran on any stall past 44 ms
    static constexpr size_t kRxBufferBytes = 2048;

    explicit Uart(const struct device* uart) : uart_(uart) {
        ring_buf_init(&rx_, sizeof(rx_storage_), rx_storage_);
    }

    Status begin() {
        if (!device_is_ready(uart_)) return Status::Ok;
        if (uart_irq_callback_user_data_set(uart_, on_rx_ready, this) != 0) return Status::Down;
        uart_irq_rx_enable(uart_);
        return Status::Ok;
    }

    uint32_t overruns() const override { return overruns_; }

    // INFO: fc 20nov26 the L76K's autobaud recovery retunes through this, and false is a refusal
    bool set(uint32_t baud) override {
        struct uart_config config{};
        if (uart_config_get(uart_, &config) != 0) return false;
        if (config.baudrate == baud) return true;
        drain(config.baudrate);
        config.baudrate = baud;
        return uart_configure(uart_, &config) == 0;
    }
    size_t write(const uint8_t* data, size_t bytes) override {
        for (size_t i = 0; i < bytes; i++) uart_poll_out(uart_, data[i]);
        return bytes;
    }
    size_t read(uint8_t* out, size_t capacity) override {
        return ring_buf_get(&rx_, out, static_cast<uint32_t>(capacity));
    }
    size_t available() override { return ring_buf_size_get(&rx_); }

   private:
    // INFO: fc 27sep26 uarte_nrfx_poll_out returns once a byte's DMA starts, not once it has left
    static constexpr uint32_t kBytesOnTheWire = 2;
    static constexpr uint32_t kBitsPerByte = 10;

    static void drain(uint32_t baud) {
        k_busy_wait(kBytesOnTheWire * kBitsPerByte * 1000000u / baud);
    }

    static void on_rx_ready(const struct device* uart, void* user_data) {
        Uart* self = static_cast<Uart*>(user_data);
        bool line_ended = false;
        for (uart_irq_update(uart); uart_irq_rx_ready(uart) == 1; uart_irq_update(uart)) {
            uint8_t chunk[16];
            const int fetched = uart_fifo_read(uart, chunk, sizeof(chunk));
            if (fetched <= 0) break;
            const uint32_t placed = ring_buf_put(&self->rx_, chunk, static_cast<uint32_t>(fetched));
            if (placed < static_cast<uint32_t>(fetched)) self->overruns_++;
            line_ended = line_ended || memchr(chunk, '\n', static_cast<size_t>(fetched)) != nullptr;
        }
        // INFO: fc 03oct26 RMC ends the second's burst: parse it now, not up to a pass later
        if (line_ended) g_loop_wake.wake();
    }

    const struct device* uart_;
    struct ring_buf rx_{};
    uint8_t rx_storage_[kRxBufferBytes]{};
    uint32_t overruns_{0};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
