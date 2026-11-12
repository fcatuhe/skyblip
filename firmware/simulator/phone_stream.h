#ifndef SKYBLIP_SIMULATOR_PHONE_STREAM_H
#define SKYBLIP_SIMULATOR_PHONE_STREAM_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include "core/events/link.h"
#include "hardware/platform/host/link.h"

namespace skyblip::simulator {

// INFO: fc 13nov26 the phone's side of the link: the newest sentences a unit sent, oldest first
class PhoneStream {
   public:
    static constexpr size_t kKeptSentences = 32;

    // INFO: fc 13nov26 drained every step, so a browser left running for hours holds 32 lines
    void read(platform::host::Link& link) {
        for (const auto& frame : link.sent)
            if (frame.endpoint == events::Endpoint::Nmea) stream_ += frame.bytes;
        link.clear();
        for (size_t end = stream_.find("\r\n"); end != std::string::npos;
             end = stream_.find("\r\n")) {
            sentences_.push_back(stream_.substr(0, end));
            stream_.erase(0, end + 2);
            sentences_read_++;
            if (sentences_.size() > kKeptSentences) sentences_.pop_front();
        }
    }

    const std::deque<std::string>& sentences() const { return sentences_; }
    uint32_t sentences_read() const { return sentences_read_; }

   private:
    std::deque<std::string> sentences_{};
    std::string stream_{};
    uint32_t sentences_read_{0};
};

}  // namespace skyblip::simulator

#endif
