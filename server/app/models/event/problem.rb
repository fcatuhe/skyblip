class Event::Problem
  SAMPLES = 20

  attr_reader :fingerprint, :count, :devices, :browsers, :first_at, :last_at, :latest

  class << self
    def since(time)
      errors = Event.errors.since(time).where.not(fingerprint: nil)
      counts = errors.group(:fingerprint).count
      devices = errors.group(:fingerprint).distinct.count(:device)
      browsers = errors.group(:fingerprint).distinct.count(:browser)
      last_ids = errors.group(:fingerprint).maximum(:id)
      latest = Event.where(id: last_ids.values).index_by(&:fingerprint)
      first = Event.errors.where(fingerprint: counts.keys).group(:fingerprint).minimum(:occurred_at)

      counts.keys.map do |fingerprint|
        new(fingerprint:, count: counts[fingerprint], devices: devices[fingerprint], browsers: browsers[fingerprint],
          first_at: first[fingerprint], latest: latest[fingerprint])
      end.sort_by(&:last_at).reverse
    end

    def find(fingerprint)
      occurrences = Event.errors.where(fingerprint:)
      if (latest = occurrences.order(:occurred_at, :id).last)
        new(fingerprint:, count: occurrences.count, devices: occurrences.distinct.count(:device), browsers: occurrences.distinct.count(:browser),
          first_at: occurrences.minimum(:occurred_at), latest:)
      end
    end
  end

  def initialize(fingerprint:, count:, devices:, browsers:, first_at:, latest:)
    @fingerprint, @count, @devices, @browsers, @first_at, @latest = fingerprint, count, devices, browsers, first_at, latest
    @last_at = latest.occurred_at
  end

  def name = latest.name

  def side = name == "browser.failed" ? "browser" : "server"

  def error = latest.payload["error"]

  def message = latest.payload["message"]

  def place = latest.payload["frames"]&.first || latest.payload["file"]

  def new_since?(time) = first_at >= time

  def occurrences(limit: SAMPLES) = Event.errors.where(fingerprint:).order(occurred_at: :desc, id: :desc).limit(limit)

  def to_h
    { fingerprint:, side:, error:, message:, place:, count:, devices:, browsers:, first_at: first_at.utc.iso8601, last_at: last_at.utc.iso8601,
      revision: latest.payload["revision"] }.compact
  end
end
