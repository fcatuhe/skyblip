class Capture < CapturesRecord
  include Contents

  DEVICE = /\A(?:go|bravo)-\h{1,8}\z/
  BROWSER = /\A[0-9A-HJKMNP-TV-Z]{8}\z/
  # INFO: fc 15nov26 a full ring is 28,900 records, about 7 MB of NDJSON and under 1 MB gzipped
  MAX_BYTES = 4.megabytes

  enum :profile, %w[full power_run flight_run].index_by(&:itself), suffix: true

  validates :device, format: { with: DEVICE }
  validates :browser, format: { with: BROWSER }, allow_nil: true
  validates :bytes, numericality: { less_than_or_equal_to: MAX_BYTES }
  validate :reads_as_a_session

  before_validation :measure, on: :create
  before_create :keep
  after_create_commit :announce

  scope :expired, -> { where(kept_until: ...Time.current) }
  scope :newest_first, -> { order(created_at: :desc) }
  scope :without_body, -> { select(column_names - [ "body" ]) }

  def self.prune = expired.in_batches.delete_all

  def filename = "#{device}-#{session}.ndjson.gz"

  private
    def measure
      self.bytes = body.to_s.bytesize
    end

    def keep
      self.kept_until = Time.current + lifetime
    end

    # INFO: fc 15nov26 a full capture holds positions and other aircraft, a run holds the device's power alone
    def lifetime
      full_profile? ? Rails.configuration.x.tracker_lifetime : Rails.configuration.x.retention
    end

    def announce
      Rails.event.notify "capture.received", device:, capture_id: id, profile:, session:, records:, bytes:, firmware:
    end
end
