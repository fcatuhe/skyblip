#ifndef SKYBLIP_CORE_SETTINGS_BLOB_H
#define SKYBLIP_CORE_SETTINGS_BLOB_H

#include <cstddef>
#include <cstdint>

#include "core/util/result.h"

namespace skyblip::settings {

constexpr size_t kBlobOverhead = 1 + 4;

constexpr size_t blob_bytes(size_t payload) { return payload + kBlobOverhead; }

constexpr uint8_t blob_version(const uint8_t* in) { return in[0]; }

// What the settings in use are, when they are not the blob a product stores.
enum class Fallback : uint8_t { None, Prior, Defaults };

const char* to_string(Fallback fallback);

void seal(uint8_t version, const void* payload, size_t payload_len, uint8_t* out, size_t cap);

Status open(const uint8_t* in, size_t len, size_t payload_len, void* payload_out);

bool sealed(const uint8_t* in, size_t len);

}  // namespace skyblip::settings

#endif
