class Unit::ReportedFirmware
  Stretch = Data.define(:version, :track, :first_seen_at, :last_seen_at)

  def initialize(unit)
    @unit = unit
  end

  def stretches
    reports.chunk_while { |earlier, later| earlier[:version] == later[:version] }.map do |run|
      Stretch.new(version: run.first[:version], track: run.filter_map { it[:track] }.last,
        first_seen_at: run.first[:at], last_seen_at: run.last[:at])
    end.reverse
  end

  private
    # INFO: fc 14nov26 any browser can post a journal naming any device, so these versions are what browsers said, not what the unit signed
    def reports
      Event.where(name: %w[device.read device.installed], device: @unit.slug).order(:occurred_at, :id)
        .pluck(:name, Event.field("payload.running"), Event.field("payload.to"), Event.field("payload.track"), :occurred_at)
        .filter_map do |name, running, installed, track, at|
          version = name == "device.read" ? running : installed
          { version:, track:, at: } if version.present?
        end
    end
end
