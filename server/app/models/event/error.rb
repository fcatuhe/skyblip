class Event::Error
  SAMPLES = 20

  attr_reader :fingerprint, :count, :devices, :browsers, :first_at, :last_at, :latest, :resolution

  class << self
    def since(time)
      errors = Event.errors.since(time).where.not(fingerprint: nil)
      counts = errors.group(:fingerprint).count
      devices = errors.group(:fingerprint).distinct.count(:device)
      browsers = errors.group(:fingerprint).distinct.count(:browser)
      latest = latest_of(errors).index_by(&:fingerprint)
      first = Event.errors.where(fingerprint: counts.keys).group(:fingerprint).minimum(:occurred_at)
      resolutions = Resolution.where(fingerprint: counts.keys).index_by(&:fingerprint)

      counts.keys.map do |fingerprint|
        new(fingerprint:, count: counts[fingerprint], devices: devices[fingerprint], browsers: browsers[fingerprint],
          first_at: first[fingerprint], latest: latest[fingerprint], resolution: resolutions[fingerprint])
      end.sort_by(&:last_at).reverse
    end

    def find_by(fingerprint:)
      occurrences = Event.errors.where(fingerprint:)
      if (latest = occurrences.order(:occurred_at, :id).last)
        new(fingerprint:, count: occurrences.count, devices: occurrences.distinct.count(:device), browsers: occurrences.distinct.count(:browser),
          first_at: occurrences.minimum(:occurred_at), latest:, resolution: Resolution.find_by(fingerprint:))
      end
    end

    private
      def latest_of(errors)
        events = Event.arel_table
        newest_first = Arel::Nodes::Window.new.partition(events[:fingerprint]).order(events[:occurred_at].desc, events[:id].desc)
        rank = Arel::Nodes::Over.new(Arel::Nodes::NamedFunction.new("ROW_NUMBER", []), newest_first).as("rank")

        Event.from(errors.select(events[Arel.star], rank).arel.as("events")).where(rank: 1)
      end
  end

  def initialize(fingerprint:, count:, devices:, browsers:, first_at:, latest:, resolution:)
    @fingerprint, @count, @devices, @browsers, @first_at, @latest, @resolution = fingerprint, count, devices, browsers, first_at, latest, resolution
    @last_at = latest.occurred_at
  end

  def name = latest.name

  def side = name == "browser.failed" ? "browser" : "server"

  def error = latest.payload["error"]

  def message = latest.payload["message"]

  def place = latest.payload["frames"]&.first || latest.payload["file"]

  def new_since?(time) = first_at >= time

  def resolved? = resolution.present? && last_at <= resolution.resolved_at

  def reopened? = resolution.present? && last_at > resolution.resolved_at

  def resolve(by:)
    @resolution = Resolution.find_or_initialize_by(fingerprint:)
    resolution.update!(resolved_at: Time.current)
    Rails.event.notify "error.resolved", fingerprint:, by_id: by.id
  end

  def reopen(by:)
    resolution&.destroy!
    @resolution = nil
    Rails.event.notify "error.reopened", fingerprint:, by_id: by.id
  end

  def occurrences(limit: SAMPLES) = Event.errors.where(fingerprint:).order(occurred_at: :desc, id: :desc).limit(limit)

  def to_h
    { fingerprint:, side:, error:, message:, place:, count:, devices:, browsers:, first_at: first_at.utc.iso8601, last_at: last_at.utc.iso8601,
      revision: latest.payload["revision"], resolved_at: resolution&.resolved_at&.utc&.iso8601, reopened: (true if reopened?) }.compact
  end
end
