// The SMP permission matrix, driven through the real dispatcher and the hook the product registers.
#include <string.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>
#include <zephyr/mgmt/mcumgr/transport/smp_dummy.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include "core/dfu/smp_policy.h"
#include "hardware/platform/zephyr/upload_gate.h"

using namespace skyblip;

namespace {

constexpr uint32_t kResponseWaitS = 3;
constexpr uint8_t kSmpHeaderBytes = 8;
constexpr uint32_t kImageMagic = 0x96f3b83d;
constexpr size_t kImageBytes = 128;
constexpr size_t kChunkBytes = 64;

struct Reply {
    bool answered{false};
    bool result_code_valid{false};
    int32_t result_code{0};
    bool offset_valid{false};
    size_t offset{0};
    bool echo_valid{false};
};

struct Body {
    uint8_t bytes[256]{};
    size_t used_bytes{0};
};

bool key_is(const zcbor_string& key, const char* name) {
    return key.len == strlen(name) && memcmp(key.value, name, key.len) == 0;
}

Reply decode(const uint8_t* payload, size_t bytes) {
    Reply reply;
    reply.answered = true;
    ZCBOR_STATE_D(decoder, 3, payload, bytes, 1, 0);
    if (!zcbor_map_start_decode(decoder)) return reply;
    while (!zcbor_array_at_end(decoder)) {
        zcbor_string key{};
        if (!zcbor_tstr_decode(decoder, &key)) break;
        if (key_is(key, "rc")) {
            reply.result_code_valid = zcbor_int32_decode(decoder, &reply.result_code);
        } else if (key_is(key, "off")) {
            reply.offset_valid = zcbor_size_decode(decoder, &reply.offset);
        } else if (key_is(key, "r")) {
            reply.echo_valid = true;
            zcbor_any_skip(decoder, nullptr);
        } else {
            zcbor_any_skip(decoder, nullptr);
        }
    }
    return reply;
}

Reply send(dfu::SmpGroup group, uint8_t command_id, dfu::SmpOperation operation, const Body& body) {
    uint8_t packet[kSmpHeaderBytes + sizeof(Body::bytes)]{};
    const uint16_t group_id = static_cast<uint16_t>(group);
    packet[0] = static_cast<uint8_t>(operation);
    packet[2] = static_cast<uint8_t>(body.used_bytes >> 8);
    packet[3] = static_cast<uint8_t>(body.used_bytes);
    packet[4] = static_cast<uint8_t>(group_id >> 8);
    packet[5] = static_cast<uint8_t>(group_id);
    packet[7] = command_id;
    memcpy(packet + kSmpHeaderBytes, body.bytes, body.used_bytes);

    smp_dummy_enable();
    smp_dummy_clear_state();
    (void)smp_dummy_tx_pkt(packet, static_cast<int>(kSmpHeaderBytes + body.used_bytes));
    smp_dummy_add_data();
    Reply reply;
    if (smp_dummy_wait_for_data(kResponseWaitS)) {
        net_buf* outgoing = smp_dummy_get_outgoing();
        if (outgoing->len >= kSmpHeaderBytes)
            reply = decode(outgoing->data + kSmpHeaderBytes, outgoing->len - kSmpHeaderBytes);
        net_buf_unref(outgoing);
    }
    smp_dummy_disable();
    return reply;
}

template <class Encode>
Body body(Encode encode) {
    Body out;
    ZCBOR_STATE_E(encoder, 2, out.bytes, sizeof(out.bytes), 0);
    const bool ok =
        zcbor_map_start_encode(encoder, 8) && encode(encoder) && zcbor_map_end_encode(encoder, 8);
    zassert_true(ok, "request did not encode");
    out.used_bytes = static_cast<size_t>(encoder->payload_mut - out.bytes);
    return out;
}

Body empty() {
    return body([](zcbor_state_t*) { return true; });
}

const uint8_t* image() {
    static uint8_t bytes[kImageBytes]{};
    memcpy(bytes, &kImageMagic, sizeof(kImageMagic));
    return bytes;
}

Reply upload_chunk(size_t offset, size_t bytes) {
    return send(dfu::SmpGroup::Image, dfu::kSmpImageUpload, dfu::SmpOperation::Write,
                body([&](zcbor_state_t* encoder) {
                    bool ok = zcbor_tstr_put_lit(encoder, "off") &&
                              zcbor_size_put(encoder, offset) &&
                              zcbor_tstr_put_lit(encoder, "data") &&
                              zcbor_bstr_encode_ptr(
                                  encoder, reinterpret_cast<const char*>(image() + offset), bytes);
                    if (offset == 0)
                        ok = ok && zcbor_tstr_put_lit(encoder, "len") &&
                             zcbor_size_put(encoder, kImageBytes);
                    return ok;
                }));
}

bool refused(const Reply& reply) {
    return reply.answered && reply.result_code_valid && reply.result_code == MGMT_ERR_EACCESSDENIED;
}

void open_gate() { platform::zephyr::UploadGate::publish(true); }

void before_each(void*) {
    platform::zephyr::UploadGate::publish(false);
    platform::zephyr::UploadGate::forget_finished();
}

bool upload_whole() {
    for (size_t offset = 0; offset < kImageBytes; offset += kChunkBytes)
        if (upload_chunk(offset, kChunkBytes).offset != offset + kChunkBytes) return false;
    return true;
}

}  // namespace

ZTEST_SUITE(smp_gate, nullptr, nullptr, before_each, nullptr, nullptr);

ZTEST(smp_gate, test_image_upload_is_refused_while_the_gate_is_closed) {
    const Reply reply = upload_chunk(0, kChunkBytes);
    zassert_true(refused(reply), "rc %d", reply.result_code);
    zassert_false(reply.offset_valid, "the handler ran");
}

ZTEST(smp_gate, test_image_upload_reaches_the_handler_while_the_gate_is_open) {
    open_gate();
    const Reply reply = upload_chunk(0, kChunkBytes);
    zassert_true(reply.answered);
    zassert_false(reply.result_code_valid && reply.result_code != MGMT_ERR_EOK, "rc %d",
                  reply.result_code);
    zassert_true(reply.offset_valid, "no offset, the handler did not run");
    zassert_equal(reply.offset, kChunkBytes);
}

ZTEST(smp_gate, test_image_state_write_is_refused_inside_the_window) {
    open_gate();
    const Reply reply =
        send(dfu::SmpGroup::Image, dfu::kSmpImageState, dfu::SmpOperation::Write,
             body([](zcbor_state_t* encoder) {
                 return zcbor_tstr_put_lit(encoder, "confirm") && zcbor_bool_put(encoder, true);
             }));
    zassert_true(refused(reply), "rc %d", reply.result_code);
}

ZTEST(smp_gate, test_image_erase_is_refused_inside_the_window) {
    open_gate();
    const Reply reply =
        send(dfu::SmpGroup::Image, dfu::kSmpImageErase, dfu::SmpOperation::Write,
             body([](zcbor_state_t* encoder) {
                 return zcbor_tstr_put_lit(encoder, "slot") && zcbor_uint32_put(encoder, 1);
             }));
    zassert_true(refused(reply), "rc %d", reply.result_code);
}

ZTEST(smp_gate, test_os_reset_is_refused_inside_the_window) {
    open_gate();
    const Reply reply = send(dfu::SmpGroup::OperatingSystem, dfu::kSmpOperatingSystemReset,
                             dfu::SmpOperation::Write, empty());
    zassert_true(refused(reply), "rc %d", reply.result_code);
}

ZTEST(smp_gate, test_os_echo_is_answered_with_the_gate_closed) {
    const Reply reply =
        send(dfu::SmpGroup::OperatingSystem, dfu::kSmpOperatingSystemEcho, dfu::SmpOperation::Write,
             body([](zcbor_state_t* encoder) {
                 return zcbor_tstr_put_lit(encoder, "d") && zcbor_tstr_put_lit(encoder, "skyblip");
             }));
    zassert_false(reply.result_code_valid, "rc %d", reply.result_code);
    zassert_true(reply.echo_valid, "no echo, the handler did not run");
}

ZTEST(smp_gate, test_image_state_read_is_answered_with_the_gate_closed) {
    const Reply reply =
        send(dfu::SmpGroup::Image, dfu::kSmpImageState, dfu::SmpOperation::Read, empty());
    zassert_true(reply.answered);
    zassert_false(refused(reply), "a read was refused");
}

ZTEST(smp_gate, test_an_upload_written_whole_is_finished) {
    open_gate();
    zassert_true(upload_whole(), "the upload did not reach its last chunk");
    zassert_true(platform::zephyr::UploadGate::finished());
}

ZTEST(smp_gate, test_an_upload_that_stopped_short_is_not_finished) {
    open_gate();
    zassert_equal(upload_chunk(0, kChunkBytes).offset, kChunkBytes);
    zassert_false(platform::zephyr::UploadGate::finished());
}

ZTEST(smp_gate, test_a_new_upload_forgets_the_one_that_finished) {
    open_gate();
    zassert_true(upload_whole());
    zassert_equal(upload_chunk(0, kChunkBytes).offset, kChunkBytes);
    zassert_false(platform::zephyr::UploadGate::finished());
}

ZTEST(smp_gate, test_a_refused_upload_finishes_nothing) {
    zassert_true(refused(upload_chunk(0, kChunkBytes)));
    zassert_false(platform::zephyr::UploadGate::finished());
}
