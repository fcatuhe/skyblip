module Unit::Reporting
  extend ActiveSupport::Concern

  NEW_REPORTED_PER_NETWORK_PER_DAY = 20

  class_methods do
    def hear_later(uuids, ip_address: nil)
      Unit::HearingJob.perform_later(uuids, ip_address) if uuids.any?
    end

    # INFO: fc 15nov26 any browser can post a journal naming any unit, so a report records what a browser said, never a signature
    def hear_now(uuids, ip_address: nil)
      Event.where(uuid: uuids).order(:id).each do |event|
        report = Unit::Report.new(event.payload.to_h.stringify_keys.slice(*Unit::Report.attribute_names))
        case event.name
        when "device.read" then report_read(report, at: event.occurred_at, ip_address:) if report.valid?(:read)
        when "device.installed" then report_install(report, at: event.occurred_at) if report.valid?(:install)
        end
      end
    end

    private
      def report_read(report, at:, ip_address:)
        transaction do
          unit = find_by(device_id: report.device_id) || heard_first(report.device_id, at, ip_address)
          unit&.read(report, at:)
        end
      end

      def report_install(report, at:)
        find_by(device_id: report.device_id)&.latest_firmware&.then do |firmware|
          if firmware.version == report.to && at >= firmware.created_at
            firmware.continue(source: "installed", at:, from_version: report.from, lasted_ms: report.lasted_ms)
          end
        end
      end

      def heard_first(device_id, at, ip_address)
        network = network_of(ip_address)
        if network.present? && reports_from(network) > NEW_REPORTED_PER_NETWORK_PER_DAY
          Rails.logger.info "Units: #{network} reported more than #{NEW_REPORTED_PER_NETWORK_PER_DAY} new units today, so #{device_id} waits"
          nil
        else
          new(device_id:, created_at: at, updated_at: at)
        end
      end

      def reports_from(network) = Rails.cache.increment("unit-reports:#{network}", 1, expires_in: 24.hours).to_i
  end

  # INFO: fc 15nov26 an activated unit signs its product and variant, and a journal posted late carries what was true before
  def read(report, at:)
    if new_record? || (!activated? && at >= updated_at)
      identify(product: report.product, variant: report.variant, version: report.version, source: "read",
        bluetooth_tag: report.bluetooth_tag, updated_at: at)
    elsif at > updated_at
      update!(updated_at: at)
    end
    if at >= (latest_firmware&.created_at || at)
      record_firmware(version: report.version, release: report.release, variant: report.variant, source: "read", at:)
    end
  end
end
