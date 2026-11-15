module Unit::Reporting
  extend ActiveSupport::Concern

  NEW_REPORTED_PER_NETWORK_PER_DAY = 20

  class_methods do
    # INFO: fc 15nov26 any browser can post a journal naming any unit, so a report records what a browser said, never a signature
    def hear(entries, ip_address: nil)
      entries.each do |entry|
        report = Unit::Report.new(entry[:payload].to_h.slice(*Unit::Report.attribute_names.map(&:to_sym)))
        case entry[:name]
        when "device.read" then report_read(report, at: entry[:occurred_at], ip_address:) if report.valid?(:read)
        when "device.installed" then report_install(report, at: entry[:occurred_at]) if report.valid?(:install)
        end
      end
    end

    def report_read(report, at:, ip_address:)
      unit = find_by(device_id: report.device_id) || heard_first(report.device_id, at, ip_address)
      unit&.read(report, at:)
    end

    def report_install(report, at:)
      find_by(device_id: report.device_id)&.latest_firmware&.then do |firmware|
        firmware.continue(source: "installed", at:, from_version: report.from, lasted_ms: report.lasted_ms) if firmware.version == report.to
      end
    end

    private
      def heard_first(device_id, at, ip_address)
        network = network_of(ip_address)
        if network.present? && reports_from(network) > NEW_REPORTED_PER_NETWORK_PER_DAY
          Rails.logger.info "Units: #{network} reported more than #{NEW_REPORTED_PER_NETWORK_PER_DAY} new units today, so #{device_id} waits"
          nil
        else
          new(device_id:, created_at: at)
        end
      end

      def reports_from(network) = Rails.cache.increment("unit-reports:#{network}", 1, expires_in: 24.hours).to_i
  end

  def read(report, at:)
    transaction do
      identify(product: report.product, variant: report.variant, version: report.version, bluetooth_tag: report.bluetooth_tag,
        updated_at: [ updated_at, at ].compact.max)
      record_firmware(version: report.version, release: report.release, variant: report.variant, source: "read", at:)
    end
  end
end
