#include <algorithm>

#include "core/dfu/image.h"
#include "core/util/cbor.h"
#include "core/util/span.h"
#include "ports/link.h"
#include "products/common/services/config_service.h"
#include "protocols/adsb/registration.h"
#include "protocols/adsb/version.h"
#include "protocols/adsl/version.h"
#include "protocols/alptas/version.h"

namespace skyblip::common {

namespace {
constexpr int kFlashFrameBytes = 64;
static_assert(dfu::HubImageReport::kDigestBytes == link::update_bytes::kImuDigest,
              "the update frame carries the whole digest the hub image report holds");
static_assert(link::update_bytes::kKey <= std::tuple_size<ports::SigningKeyHash>::value,
              "the update frame's key is a prefix of the signing key's hash");
static_assert(link::identity_bytes::kDeviceId == identity::kDeviceIdBytes,
              "the identity frame carries the whole DEVICEID");

struct StackVersion {
    comms::RadioProtocol protocol;
    uint32_t key;
    const char* version;
};

constexpr StackVersion kStackVersions[] = {
    {comms::RadioProtocol::Adsb, link::protocols_key::kAdsbVersion, adsb::kVersion},
    {comms::RadioProtocol::Adsl, link::protocols_key::kAdslVersion, adsl::kVersion},
    {comms::RadioProtocol::Alptas, link::protocols_key::kAlptasVersion, alptas::kVersion},
};
}  // namespace

// INFO: fc 04aug26 pushed unsolicited, so it carries state only and fits at its worst: README.md
int ConfigService::format_status(uint8_t* out, int capacity) {
    cbor::Writer writer(out, capacity);
    writer.add_code(link::kCommandKey, link::Command::Status);
    writer.add_code(link::status_key::kFlight, flight_state_);
    writer.add_boolean(link::status_key::kUpload, upload_allowed());
    const power::BatteryState& battery = diagnostics_.battery;
    if (battery.valid) writer.add_unsigned(link::status_key::kBatteryPercent, battery.percent);
    writer.add_boolean(link::status_key::kCharging, battery.charging);
    writer.add_code(link::status_key::kPowerLevel, diagnostics_.power_level);
    writer.add_boolean(link::status_key::kWentDarkFlat, went_dark_flat_);
    if (bravo_ != comms::BravoState::Unpaired) writer.add_code(link::status_key::kBravo, bravo_);
    const comms::OwnTransponderHeard heard =
        own_transponder_.value_or(comms::OwnTransponderHeard{});
    char registration[comms::kPeerCallsignBytes]{};
    adsb::registration(registration, sizeof(registration), heard.callsign, heard.address);
    if (own_transponder_) writer.add_unsigned(link::status_key::kOwnTransponder, heard.address);
    if (registration[0] != 0)
        writer.add_text(link::status_key::kOwnTransponderCallsign, registration);
    if (heard.aircraft_category != 0)
        writer.add_unsigned(link::status_key::kOwnTransponderCategory, heard.aircraft_category);
    if (no_tx_ != timing::NoTx::None) writer.add_code(link::status_key::kNoTx, no_tx_);
    return writer.finish();
}

void ConfigService::send_status() {
    uint8_t frame[comms::kSmallestSupportedPayloadBytes];
    const int bytes = format_status(frame, static_cast<int>(sizeof(frame)));
    if (bytes == 0) {
        diagnostics_.link_drops++;
        status_push_due_ = false;
        return;
    }
    (void)reply(frame, bytes);
}

void ConfigService::send_defaults() {
    uint8_t frame[comms::kSmallestSupportedPayloadBytes];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Defaults);
    store_.write_default_fields(writer);
    const int bytes = writer.finish();
    if (bytes == 0) {
        diagnostics_.link_drops++;
        return;
    }
    (void)reply(frame, bytes);
}

// INFO: fc 18sep26 Nobody asked for this one, so every app subscribed to it gets it.
void ConfigService::push_status() {
    uint8_t frame[comms::kSmallestSupportedPayloadBytes];
    const int bytes = format_status(frame, static_cast<int>(sizeof(frame)));
    if (bytes == 0) {
        diagnostics_.link_drops++;
        status_push_due_ = false;
        return;
    }
    status_push_due_ = channel_.broadcast(frame, bytes) == Status::WouldBlock;
}

// INFO: fc 04aug26 several frames, all or none, because trimming a histogram destroys its evidence
void ConfigService::send_timing() {
    if (timing_statistics_ == nullptr) {
        acknowledge(false, link::Reason::NoStatistics);
        return;
    }
    channel_.start_timing(*timing_statistics_);
}

void ConfigService::send_flash() {
    if (durable_writes_ == nullptr) {
        acknowledge(false, link::Reason::NoStatistics);
        return;
    }
    uint8_t frame[kFlashFrameBytes];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Flash);
    writer.add_boolean(link::flash_key::kWritable, settings_writable());
    writer.add_unsigned(link::flash_key::kRequests, durable_writes_->requests());
    writer.add_unsigned(link::flash_key::kWrites, durable_writes_->writes());
    writer.add_unsigned(link::flash_key::kForced, durable_writes_->forced());
    writer.add_unsigned(link::flash_key::kWorstWaitMs, durable_writes_->worst_wait_ms());
    writer.add_boolean(link::flash_key::kPending, durable_writes_->pending());
    writer.add_unsigned(link::flash_key::kBudgetMs, timing::DurableWriteWindow::kWorstWriteMs);
    writer.add_unsigned(link::flash_key::kBoundMs, timing::DurableWriteWindow::kDeferCeilingMs);
    (void)reply(frame, writer.finish());
}

void ConfigService::send_radio() {
    channel_.start_diagnostics(diagnostics_, link::Command::Radio,
                               comms::DiagnosticsReport::Group::Radio);
}

void ConfigService::send_update() {
    uint8_t frame[comms::kSmallestSupportedPayloadBytes];
    const int bytes = format_update(frame, static_cast<int>(sizeof(frame)));
    if (bytes == 0) {
        diagnostics_.link_drops++;
        return;
    }
    (void)reply(frame, bytes);
}

void ConfigService::send_install_report() {
    uint8_t frame[comms::kSmallestSupportedPayloadBytes];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::InstallReport);
    if (install_report_ != nullptr) {
        const dfu::InstallReport& report = *install_report_;
        writer.add_code(link::install_report_key::kCheck, report.request.check);
        writer.add_code(link::install_report_key::kSwap, report.request.swap);
        writer.add_integer(link::install_report_key::kRequestError, report.request.error);
        writer.add_unsigned(link::install_report_key::kBoots, report.boots);
        writer.add_unsigned(link::install_report_key::kProbationBoots, report.probation_boots);
        writer.add_code(link::install_report_key::kProof, report.proof);
        writer.add_code(link::install_report_key::kReset, report.reset);
    }
    if (landing_) {
        writer.add_code(link::install_report_key::kLandedCheck, landing_->check);
        if (landing_->staged_key)
            writer.add_bytes(link::install_report_key::kStagedKey,
                             ConstByteSpan(landing_->staged_key->data(),
                                           link::install_report_bytes::kStagedKey));
    }
    (void)reply(frame, writer.finish());
}

void ConfigService::send_protocols() {
    uint8_t frame[comms::kSmallestSupportedPayloadBytes];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Protocols);
    for (const StackVersion& stack : kStackVersions)
        if (carries(stack.protocol)) writer.add_text(stack.key, stack.version);
    (void)reply(frame, writer.finish());
}

void ConfigService::send_identity() {
    char version[dfu::kVersionTextBytes] = {0};
    if (!running_version_text(version, sizeof(version))) {
        refuse(link::Reason::NoVersion);
        return;
    }
    const identity::Unit& unit = identity_->unit();
    uint8_t frame[comms::IdentityLink::kIdentityFrameBytes];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Identity);
    writer.add_code(link::identity_key::kProduct, product_);
    writer.add_code(link::identity_key::kVariant, variant_);
    writer.add_text(link::identity_key::kVersion, version);
    writer.add_boolean(link::identity_key::kRelease, dfu_ != nullptr && dfu_->release_build());
    writer.add_bytes(link::identity_key::kDeviceId,
                     ConstByteSpan(unit.device_id().data(), link::identity_bytes::kDeviceId));
    writer.add_unsigned(link::identity_key::kBluetoothTag, unit.activation().bluetooth_tag);
    const int bytes = writer.finish();
    if (bytes == 0) {
        diagnostics_.link_drops++;
        return;
    }
    (void)reply(frame, bytes);
}

bool ConfigService::carries(comms::RadioProtocol protocol) const {
    return std::any_of(radio_protocols_.begin(), radio_protocols_.end(),
                       [protocol](comms::RadioProtocol carried) { return carried == protocol; });
}

void ConfigService::push_update(const events::Peer& peer) {
    uint8_t frame[comms::kSmallestSupportedPayloadBytes];
    const int bytes = format_update(frame, static_cast<int>(sizeof(frame)));
    if (bytes == 0) {
        diagnostics_.link_drops++;
        return;
    }
    (void)channel_.deliver(peer, frame, bytes);
}

int ConfigService::format_update(uint8_t* out, int capacity) {
    char from[dfu::kVersionTextBytes];
    char to[dfu::kVersionTextBytes];
    dfu::format_version(update_record_.from, from, sizeof(from));
    dfu::format_version(update_record_.to, to, sizeof(to));
    cbor::Writer writer(out, capacity);
    writer.add_code(link::kCommandKey, link::Command::Update);
    writer.add_code(link::update_key::kImage, image_state_);
    if (image_state_ != dfu::ImageState::Confirmed) {
        writer.add_text(link::update_key::kFrom, from);
        writer.add_text(link::update_key::kTo, to);
    }
    if (settings_fallback_ != settings::Fallback::None)
        writer.add_code(link::update_key::kSettings, settings_fallback_);
    writer.add_boolean(link::update_key::kSwapPowered, swap_powered());
    writer.add_code(link::update_key::kImu, hub_image_.holding);
    if (hub_image_.holding == dfu::HubImage::Held)
        writer.add_bytes(link::update_key::kImuDigest,
                         ConstByteSpan(hub_image_.digest, link::update_bytes::kImuDigest));
    if (dfu_ != nullptr && dfu_->downgrade_allowed())
        writer.add_boolean(link::update_key::kDowngrade, true);
    ports::SigningKeyHash trusted;
    if (dfu_ != nullptr && dfu_->running_key(trusted))
        writer.add_bytes(link::update_key::kKey,
                         ConstByteSpan(trusted.data(), link::update_bytes::kKey));
    ports::BootloaderKeys bootloader;
    if (dfu_ != nullptr && dfu_->bootloader_keys(bootloader)) {
        uint8_t ids[ports::BootloaderKeys::kCapacity * link::update_bytes::kKey];
        for (size_t i = 0; i < bootloader.count; i++)
            std::copy_n(bootloader.keys[i].data(), link::update_bytes::kKey,
                        ids + i * link::update_bytes::kKey);
        writer.add_bytes(link::update_key::kBootloaderKeys,
                         ConstByteSpan(ids, bootloader.count * link::update_bytes::kKey));
    }
    return writer.finish();
}

// INFO: fc 20nov26 refused until collected, because a dump of zeros reports health nobody measured
void ConfigService::send_diagnostics() {
    if (diagnostics_.refreshes == 0) {
        acknowledge(false, link::Reason::NoStatistics);
        return;
    }
    channel_.start_diagnostics(diagnostics_, link::Command::Diagnostics);
}

}  // namespace skyblip::common
