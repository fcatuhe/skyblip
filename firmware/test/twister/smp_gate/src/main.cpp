// The SMP permission matrix, driven through the real dispatcher and the hook the product registers.
#include <string.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>
#include <zephyr/mgmt/mcumgr/transport/smp_dummy.h>
#include <zephyr/net_buf.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>

#include "core/dfu/smp_policy.h"
#include "hardware/platform/zephyr/upload_gate.h"

using namespace skyblip;

namespace {

constexpr uint32_t kResponseWaitS = 3;
constexpr uint8_t kSmpHeaderBytes = 8;
constexpr uint8_t kSmpVersion2 = 1 << 3;
constexpr size_t kImageBytes = 128;
constexpr size_t kChunkBytes = 64;
constexpr uint8_t kGo = 0x11;
constexpr uint8_t kBravo = 0x22;
constexpr uint8_t kNoProduct = 0;

struct Reply {
    bool answered{false};
    bool result_code_valid{false};
    int32_t result_code{0};
    bool offset_valid{false};
    size_t offset{0};
    bool echo_valid{false};
    bool image_error_valid{false};
    uint32_t image_error{0};
};

struct Body {
    uint8_t bytes[256]{};
    size_t used_bytes{0};
};

bool key_is(const zcbor_string& key, const char* name) {
    return key.len == strlen(name) && memcmp(key.value, name, key.len) == 0;
}

bool decode_image_error(zcbor_state_t* decoder, Reply& reply) {
    if (!zcbor_map_start_decode(decoder)) return false;
    uint32_t group = UINT32_MAX;
    while (!zcbor_array_at_end(decoder)) {
        zcbor_string key{};
        if (!zcbor_tstr_decode(decoder, &key)) return false;
        if (key_is(key, "group")) {
            if (!zcbor_uint32_decode(decoder, &group)) return false;
        } else if (key_is(key, "rc")) {
            if (!zcbor_uint32_decode(decoder, &reply.image_error)) return false;
        } else {
            zcbor_any_skip(decoder, nullptr);
        }
    }
    return zcbor_map_end_decode(decoder) && group == MGMT_GROUP_ID_IMAGE;
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
        } else if (key_is(key, "err")) {
            reply.image_error_valid = decode_image_error(decoder, reply);
        } else if (key_is(key, "r")) {
            reply.echo_valid = true;
            zcbor_any_skip(decoder, nullptr);
        } else {
            zcbor_any_skip(decoder, nullptr);
        }
    }
    return reply;
}

Reply send(dfu::SmpGroup group, uint8_t command_id, dfu::SmpOperation operation, const Body& body,
           uint8_t version = 0) {
    uint8_t packet[kSmpHeaderBytes + sizeof(Body::bytes)]{};
    const uint16_t group_id = static_cast<uint16_t>(group);
    packet[0] = static_cast<uint8_t>(version | static_cast<uint8_t>(operation));
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

struct Image {
    uint8_t bytes[kImageBytes]{};
};

// 32 bytes of header and 32 of body, then 24 of protected TLVs naming the product and 40 of hash.
Image image(uint8_t product) {
    Image out;
    const uint8_t header[] = {0x3d, 0xb8, 0xf3, 0x96, 0, 0, 0, 0, 32, 0, 0, 0, 32};
    memcpy(out.bytes, header, sizeof(header));
    uint8_t* at = out.bytes + 64;
    if (product != kNoProduct) {
        out.bytes[10] = 24;
        const uint8_t protected_area[] = {0x08, 0x69, 24, 0, 0x75, 0, 16, 0};
        memcpy(at, protected_area, sizeof(protected_area));
        memset(at + sizeof(protected_area), product, 16);
        at += 24;
    }
    const uint8_t hash[] = {0x07, 0x69, 40, 0, 0x10, 0, 32, 0};
    memcpy(at, hash, sizeof(hash));
    memset(at + sizeof(hash), 0xaa, 32);
    return out;
}

void run_from_slot0(uint8_t product) {
    const flash_area* area = nullptr;
    zassert_ok(flash_area_open(PARTITION_ID(slot0_partition), &area));
    zassert_ok(flash_area_erase(area, 0, area->fa_size));
    zassert_ok(flash_area_write(area, 0, image(product).bytes, kImageBytes));
    flash_area_close(area);
}

Reply upload_chunk(size_t offset, size_t bytes, uint8_t product = kGo) {
    const Image uploaded = image(product);
    return send(dfu::SmpGroup::Image, dfu::kSmpImageUpload, dfu::SmpOperation::Write,
                body([&](zcbor_state_t* encoder) {
                    bool ok =
                        zcbor_tstr_put_lit(encoder, "off") && zcbor_size_put(encoder, offset) &&
                        zcbor_tstr_put_lit(encoder, "data") &&
                        zcbor_bstr_encode_ptr(
                            encoder, reinterpret_cast<const char*>(uploaded.bytes + offset), bytes);
                    if (offset == 0)
                        ok = ok && zcbor_tstr_put_lit(encoder, "len") &&
                             zcbor_size_put(encoder, kImageBytes);
                    return ok;
                }),
                kSmpVersion2);
}

bool refused_as_another_product(const Reply& reply) {
    return reply.image_error_valid && reply.image_error == IMG_MGMT_ERR_INVALID_TLV;
}

bool refused(const Reply& reply) {
    return reply.answered && reply.result_code_valid && reply.result_code == MGMT_ERR_EACCESSDENIED;
}

void open_gate() { platform::zephyr::UploadGate::publish(true); }

void before_each(void*) {
    platform::zephyr::UploadGate::publish(false);
    platform::zephyr::UploadGate::forget_finished();
    run_from_slot0(kGo);
}

bool upload_whole() {
    for (size_t offset = 0; offset < kImageBytes; offset += kChunkBytes) {
        const Reply reply = upload_chunk(offset, kChunkBytes);
        if (reply.image_error_valid || reply.offset != offset + kChunkBytes) return false;
    }
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

ZTEST(smp_gate, test_an_image_for_another_product_is_refused_before_its_last_chunk_lands) {
    open_gate();
    zassert_false(upload_chunk(0, kChunkBytes, kBravo).image_error_valid);
    const Reply reply = upload_chunk(kChunkBytes, kChunkBytes, kBravo);
    zassert_true(refused_as_another_product(reply), "err %u", reply.image_error);
    zassert_false(platform::zephyr::UploadGate::finished());
}

ZTEST(smp_gate, test_an_image_that_names_no_product_is_refused_at_its_first_chunk) {
    open_gate();
    const Reply reply = upload_chunk(0, kChunkBytes, kNoProduct);
    zassert_true(refused_as_another_product(reply), "err %u", reply.image_error);
}

ZTEST(smp_gate, test_a_unit_running_an_image_that_names_no_product_takes_none) {
    run_from_slot0(kNoProduct);
    open_gate();
    const Reply reply = upload_chunk(0, kChunkBytes);
    zassert_true(refused_as_another_product(reply), "err %u", reply.image_error);
}
