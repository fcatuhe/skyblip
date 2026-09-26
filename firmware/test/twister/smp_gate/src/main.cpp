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
    bool has_rc{false};
    int32_t rc{0};
    bool has_off{false};
    size_t off{0};
    bool has_echo{false};
};

struct Body {
    uint8_t bytes[256]{};
    size_t len{0};
};

bool key_is(const zcbor_string& key, const char* name) {
    return key.len == strlen(name) && memcmp(key.value, name, key.len) == 0;
}

Reply decode(const uint8_t* payload, size_t len) {
    Reply reply;
    reply.answered = true;
    ZCBOR_STATE_D(zsd, 3, payload, len, 1, 0);
    if (!zcbor_map_start_decode(zsd)) return reply;
    while (!zcbor_array_at_end(zsd)) {
        zcbor_string key{};
        if (!zcbor_tstr_decode(zsd, &key)) break;
        if (key_is(key, "rc")) {
            reply.has_rc = zcbor_int32_decode(zsd, &reply.rc);
        } else if (key_is(key, "off")) {
            reply.has_off = zcbor_size_decode(zsd, &reply.off);
        } else if (key_is(key, "r")) {
            reply.has_echo = true;
            zcbor_any_skip(zsd, nullptr);
        } else {
            zcbor_any_skip(zsd, nullptr);
        }
    }
    return reply;
}

Reply send(dfu::SmpGroup group, uint8_t id, dfu::SmpOp op, const Body& body) {
    uint8_t packet[kSmpHeaderBytes + sizeof(Body::bytes)]{};
    const uint16_t group_id = static_cast<uint16_t>(group);
    packet[0] = static_cast<uint8_t>(op);
    packet[2] = static_cast<uint8_t>(body.len >> 8);
    packet[3] = static_cast<uint8_t>(body.len);
    packet[4] = static_cast<uint8_t>(group_id >> 8);
    packet[5] = static_cast<uint8_t>(group_id);
    packet[7] = id;
    memcpy(packet + kSmpHeaderBytes, body.bytes, body.len);

    smp_dummy_enable();
    smp_dummy_clear_state();
    (void)smp_dummy_tx_pkt(packet, static_cast<int>(kSmpHeaderBytes + body.len));
    smp_dummy_add_data();
    Reply reply;
    if (smp_dummy_wait_for_data(kResponseWaitS)) {
        net_buf* nb = smp_dummy_get_outgoing();
        if (nb->len >= kSmpHeaderBytes)
            reply = decode(nb->data + kSmpHeaderBytes, nb->len - kSmpHeaderBytes);
        net_buf_unref(nb);
    }
    smp_dummy_disable();
    return reply;
}

template <class Encode>
Body body(Encode encode) {
    Body out;
    ZCBOR_STATE_E(zse, 2, out.bytes, sizeof(out.bytes), 0);
    const bool ok = zcbor_map_start_encode(zse, 8) && encode(zse) && zcbor_map_end_encode(zse, 8);
    zassert_true(ok, "request did not encode");
    out.len = static_cast<size_t>(zse->payload_mut - out.bytes);
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

Reply upload_chunk(size_t off, size_t len) {
    return send(
        dfu::SmpGroup::Image, dfu::kSmpImageUpload, dfu::SmpOp::Write,
        body([&](zcbor_state_t* zse) {
            bool ok = zcbor_tstr_put_lit(zse, "off") && zcbor_size_put(zse, off) &&
                      zcbor_tstr_put_lit(zse, "data") &&
                      zcbor_bstr_encode_ptr(zse, reinterpret_cast<const char*>(image() + off), len);
            if (off == 0)
                ok = ok && zcbor_tstr_put_lit(zse, "len") && zcbor_size_put(zse, kImageBytes);
            return ok;
        }));
}

bool refused(const Reply& reply) {
    return reply.answered && reply.has_rc && reply.rc == MGMT_ERR_EACCESSDENIED;
}

void open_gate() { platform::zephyr::UploadGate::publish(true); }

void before_each(void*) { platform::zephyr::UploadGate::publish(false); }

}  // namespace

ZTEST_SUITE(smp_gate, nullptr, nullptr, before_each, nullptr, nullptr);

ZTEST(smp_gate, test_image_upload_is_refused_while_the_gate_is_closed) {
    const Reply reply = upload_chunk(0, kChunkBytes);
    zassert_true(refused(reply), "rc %d", reply.rc);
    zassert_false(reply.has_off, "the handler ran");
}

ZTEST(smp_gate, test_image_upload_reaches_the_handler_while_the_gate_is_open) {
    open_gate();
    const Reply reply = upload_chunk(0, kChunkBytes);
    zassert_true(reply.answered);
    zassert_false(reply.has_rc && reply.rc != MGMT_ERR_EOK, "rc %d", reply.rc);
    zassert_true(reply.has_off, "no offset, the handler did not run");
    zassert_equal(reply.off, kChunkBytes);
}

ZTEST(smp_gate, test_image_state_write_is_refused_inside_the_window) {
    open_gate();
    const Reply reply = send(
        dfu::SmpGroup::Image, dfu::kSmpImageState, dfu::SmpOp::Write, body([](zcbor_state_t* zse) {
            return zcbor_tstr_put_lit(zse, "confirm") && zcbor_bool_put(zse, true);
        }));
    zassert_true(refused(reply), "rc %d", reply.rc);
}

ZTEST(smp_gate, test_image_erase_is_refused_inside_the_window) {
    open_gate();
    const Reply reply = send(dfu::SmpGroup::Image, dfu::kSmpImageErase, dfu::SmpOp::Write,
                             body([](zcbor_state_t* zse) {
                                 return zcbor_tstr_put_lit(zse, "slot") && zcbor_uint32_put(zse, 1);
                             }));
    zassert_true(refused(reply), "rc %d", reply.rc);
}

ZTEST(smp_gate, test_os_reset_is_refused_inside_the_window) {
    open_gate();
    const Reply reply = send(dfu::SmpGroup::Os, dfu::kSmpOsReset, dfu::SmpOp::Write, empty());
    zassert_true(refused(reply), "rc %d", reply.rc);
}

ZTEST(smp_gate, test_os_echo_is_answered_with_the_gate_closed) {
    const Reply reply =
        send(dfu::SmpGroup::Os, dfu::kSmpOsEcho, dfu::SmpOp::Write, body([](zcbor_state_t* zse) {
                 return zcbor_tstr_put_lit(zse, "d") && zcbor_tstr_put_lit(zse, "skyblip");
             }));
    zassert_false(reply.has_rc, "rc %d", reply.rc);
    zassert_true(reply.has_echo, "no echo, the handler did not run");
}

ZTEST(smp_gate, test_image_state_read_is_answered_with_the_gate_closed) {
    const Reply reply = send(dfu::SmpGroup::Image, dfu::kSmpImageState, dfu::SmpOp::Read, empty());
    zassert_true(reply.answered);
    zassert_false(refused(reply), "a read was refused");
}
