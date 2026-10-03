#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt.h>
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>

#include "core/dfu/image.h"
#include "core/dfu/smp_policy.h"
#include "core/dfu/update.h"
#include "hardware/platform/zephyr/dfu.h"
#include "hardware/platform/zephyr/upload_gate.h"

namespace skyblip::platform::zephyr {
namespace {

static_assert(static_cast<uint8_t>(dfu::SmpOp::Read) == MGMT_OP_READ);
static_assert(static_cast<uint8_t>(dfu::SmpOp::Write) == MGMT_OP_WRITE);
static_assert(static_cast<uint16_t>(dfu::SmpGroup::Os) == MGMT_GROUP_ID_OS);
static_assert(static_cast<uint16_t>(dfu::SmpGroup::Image) == MGMT_GROUP_ID_IMAGE);
static_assert(dfu::kSmpOsEcho == OS_MGMT_ID_ECHO);
static_assert(dfu::kSmpOsReset == OS_MGMT_ID_RESET);
static_assert(dfu::kSmpImageState == IMG_MGMT_ID_STATE);
static_assert(dfu::kSmpImageUpload == IMG_MGMT_ID_UPLOAD);
static_assert(dfu::kSmpImageErase == IMG_MGMT_ID_ERASE);

mgmt_cb_return refuse(int32_t* rc, bool* abort_more) {
    *rc = MGMT_ERR_EACCESSDENIED;
    *abort_more = true;
    return MGMT_CB_ERROR_RC;
}

// INFO: fc 07sep26 img_mgmt state-write and erase hooks only notify, so every write is gated here
mgmt_cb_return on_command(uint32_t event, mgmt_cb_return, int32_t* rc, uint16_t*, bool* abort_more,
                          void* data, size_t data_size) {
    if (event != MGMT_EVT_OP_CMD_RECV || data == nullptr ||
        data_size != sizeof(mgmt_evt_op_cmd_arg))
        return refuse(rc, abort_more);
    const auto* received = static_cast<const mgmt_evt_op_cmd_arg*>(data);
    const dfu::SmpCommand command{received->group, received->id, received->op};
    return dfu::smp_permitted(command, UploadGate::allowed()) ? MGMT_CB_OK : refuse(rc, abort_more);
}

// INFO: fc 26sep26 img_mgmt raises PENDING only once the last chunk is on flash
mgmt_cb_return on_upload(uint32_t event, mgmt_cb_return, int32_t*, uint16_t*, bool*, void*,
                         size_t) {
    if (event == MGMT_EVT_OP_IMG_MGMT_DFU_PENDING)
        UploadGate::note_finished();
    else
        UploadGate::forget_finished();
    return MGMT_CB_OK;
}

// INFO: fc 03oct26 the version travels in the first chunk, so an older image is refused before the
// rest
mgmt_cb_return on_chunk(uint32_t event, mgmt_cb_return, int32_t* rc, uint16_t* group,
                        bool* abort_more, void* data, size_t data_size) {
    if (event != MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK || data == nullptr ||
        data_size != sizeof(img_mgmt_upload_check))
        return refuse(rc, abort_more);
    const img_mgmt_upload_req& request = *static_cast<const img_mgmt_upload_check*>(data)->req;
    dfu::ImageHeader incoming;
    ports::ImageVersion running;
    Dfu slots;
    if (request.off != 0 ||
        !dfu::read_header(request.img_data.value, request.img_data.len, incoming) ||
        !slots.running_version(running) ||
        dfu::version_refusal(running, incoming.version, slots.downgrade_allowed()) == nullptr)
        return MGMT_CB_OK;
    *rc = IMG_MGMT_ERR_CURRENT_VERSION_IS_NEWER;
    *group = MGMT_GROUP_ID_IMAGE;
    *abort_more = true;
    return MGMT_CB_ERROR_ERR;
}

mgmt_callback g_command_callback{};
mgmt_callback g_upload_callback{};
mgmt_callback g_chunk_callback{};

// INFO: fc 23sep26 installed before main() and before Bluetooth, while the gate is still closed
int register_hooks() {
    g_command_callback.callback = on_command;
    g_command_callback.event_id = MGMT_EVT_OP_CMD_RECV;
    mgmt_callback_register(&g_command_callback);
    g_upload_callback.callback = on_upload;
    g_upload_callback.event_id = MGMT_EVT_OP_IMG_MGMT_DFU_STARTED |
                                 MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED |
                                 MGMT_EVT_OP_IMG_MGMT_DFU_PENDING;
    mgmt_callback_register(&g_upload_callback);
    g_chunk_callback.callback = on_chunk;
    g_chunk_callback.event_id = MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK;
    mgmt_callback_register(&g_chunk_callback);
    return 0;
}

SYS_INIT(register_hooks, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

}  // namespace
}  // namespace skyblip::platform::zephyr
