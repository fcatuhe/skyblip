#ifndef SKYBLIP_CORE_UTIL_RESULT_H
#define SKYBLIP_CORE_UTIL_RESULT_H

#include <cstdint>

#include "protocols/common/status.h"

namespace skyblip {

enum class [[nodiscard]] Status : uint8_t {
    Ok = 0,
    Invalid,
    OutOfRange,
    Crc,
    Unsupported,
    WouldBlock,
    Down,
    Timeout,
    Full,
    Empty,
    NotFound,
    Conflict,
};

constexpr bool is_ok(Status status) { return status == Status::Ok; }
constexpr const char* to_string(Status status) {
    switch (status) {
        case Status::Ok: return "Ok";
        case Status::Invalid: return "Invalid";
        case Status::OutOfRange: return "OutOfRange";
        case Status::Crc: return "Crc";
        case Status::Unsupported: return "Unsupported";
        case Status::WouldBlock: return "WouldBlock";
        case Status::Down: return "Down";
        case Status::Timeout: return "Timeout";
        case Status::Full: return "Full";
        case Status::Empty: return "Empty";
        case Status::NotFound: return "NotFound";
        case Status::Conflict: return "Conflict";
    }
    return "?";
}

constexpr Status status_of(wire::Status status) {
    switch (status) {
        case wire::Status::Ok: return Status::Ok;
        case wire::Status::Invalid: return Status::Invalid;
        case wire::Status::Crc: return Status::Crc;
        case wire::Status::Unsupported: return Status::Unsupported;
        case wire::Status::Full: return Status::Full;
    }
    return Status::Invalid;
}

template <typename T>
class Result {
   public:
    constexpr Result(T value) : value_(value), status_(Status::Ok) {}
    constexpr Result(Status error) : value_{}, status_(error) {}

    constexpr bool ok() const { return status_ == Status::Ok; }
    constexpr Status status() const { return status_; }
    constexpr const T& value() const { return value_; }
    constexpr T value_or(T fallback) const { return ok() ? value_ : fallback; }

   private:
    T value_;
    Status status_;
};

}

#endif
