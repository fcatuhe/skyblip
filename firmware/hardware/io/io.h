#ifndef SKYBLIP_HARDWARE_IO_IO_H
#define SKYBLIP_HARDWARE_IO_IO_H

#include <cstddef>
#include <cstdint>

namespace skyblip::io {

enum class BusId : uint8_t { Radio, Epd, Gnss, Sensor };

enum class PinFunction : uint8_t {
    RadioBusy,
    RadioReset,
    RadioDio1,
    EpdDc,
    EpdReset,
    EpdBusy,
    EpdBacklight,
    HapticEnable,
    GnssReset,
};

struct PinRole {
    PinFunction function;
    int pin;
};

// INFO: fc 20nov26 only the host platform reads this, to route each pin to its part model
struct PinMap {
    const PinRole* roles;
    int count;
};

class Gpio {
   public:
    virtual ~Gpio() = default;
    virtual void set(int pin, bool level) = 0;
    virtual bool get(int pin) = 0;
    virtual void mode_output(int pin) = 0;
    virtual void mode_input(int pin, bool pullup) = 0;
};

class Delay {
   public:
    virtual ~Delay() = default;
    virtual void wait_at_least_us(uint32_t hold_us) = 0;
};

class Spi {
   public:
    virtual ~Spi() = default;
    virtual void transfer(const uint8_t* tx, uint8_t* rx, size_t bytes) = 0;
    virtual void select(bool on) = 0;
};

class I2c {
   public:
    virtual ~I2c() = default;
    virtual bool write(uint8_t address, const uint8_t* data, size_t bytes) = 0;
    virtual bool read(uint8_t address, uint8_t* out, size_t bytes) = 0;
};

// INFO: fc 11nov26 8N1 puts ten bits on the wire for every byte
constexpr uint32_t wire_ms(uint32_t bytes, uint32_t baud) { return bytes * 10 * 1000 / baud; }

class Uart {
   public:
    virtual ~Uart() = default;
    virtual size_t write(const uint8_t* data, size_t bytes) = 0;
    virtual size_t read(uint8_t* out, size_t capacity) = 0;
    virtual size_t available() = 0;
    virtual uint32_t overruns() const { return 0; }
};

// INFO: fc 20nov26 here, not beside its one driver, because a platform must not include a part to
// fill a seam
class UartRate {
   public:
    virtual ~UartRate() = default;
    // INFO: fc 20nov26 false means this port cannot change rate, and the caller must act on it
    virtual bool set(uint32_t baud) = 0;
};

class FixedUartRate : public UartRate {
   public:
    bool set(uint32_t) override { return false; }
};

inline FixedUartRate kFixedUartRate{};

}  // namespace skyblip::io

#endif
