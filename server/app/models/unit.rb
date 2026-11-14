class Unit < ApplicationRecord
  # TODO: fc 14nov26 the column goes once a deploy runs without reading it, then this line goes too
  self.ignored_columns += %w[enrolled_from]

  MANUFACTURER_PREFIX = 0x00
  # INFO: fc 18oct26 serial 0 would put 0x000000 on the air, which decoders read as no address
  SERIALS = 1..0xFFFF
  NEW_PER_DAY = 50
  NEW_PER_NETWORK_PER_DAY = 5
  PENDING_KEYS_KEPT = 5

  has_many :keys, dependent: :destroy
  has_one :active_key, -> { active }, class_name: "Unit::Key"
  has_many :firmwares, dependent: :destroy
  has_one :latest_firmware, -> { newest_first }, class_name: "Unit::Firmware"

  validates :device_id, uniqueness: true, format: { with: Activation::DEVICE_ID }
  validates :serial, uniqueness: true, inclusion: { in: SERIALS }

  scope :enrolled_today, -> { where(created_at: Time.current.all_day) }

  def self.register(device_id:, public_key:, ip_address: nil, **seen)
    transaction do
      unit = find_by(device_id:) || enroll(device_id, network_of(ip_address))
      unit.bind(public_key, **seen)
    end
  end

  def self.find_by_slug(slug)
    if (serial = slug.to_s[/\Ago-#{format("%02x", MANUFACTURER_PREFIX)}(\h{4})\z/, 1])
      find_by(serial: serial.hex)
    end
  end

  def self.daily_cap_reached? = enrolled_today.count >= NEW_PER_DAY

  def self.enroll(device_id, network)
    raise Activation::Refusal.new(:daily_cap) if daily_cap_reached?
    raise Activation::Refusal.new(:network_cap) if over_network_cap?(network)

    unit = create!(device_id:, serial: next_serial)
    Rails.event.notify "unit.enrolled", unit_id: unit.id, serial: unit.serial, device: unit.slug
    Rails.logger.error "Activation: #{NEW_PER_DAY} new units today, the daily cap, so new ones wait for midnight UTC" if daily_cap_reached?
    unit
  end

  def self.over_network_cap?(network) = network.present? && enrollments_from(network) > NEW_PER_NETWORK_PER_DAY

  # INFO: fc 14nov26 counted as Rails' rate_limit counts, in the cache for 24 hours from the network's first new unit, never in the database
  def self.enrollments_from(network) = Rails.cache.increment("unit-enrollments:#{network}", 1, expires_in: 24.hours).to_i

  # INFO: fc 08nov26 a home IPv6 line holds a whole /56, so a network is that /56, or one IPv4 address
  def self.network_of(ip_address)
    if ip_address.present?
      address = IPAddr.new(ip_address)
      address = address.native if address.ipv4_mapped?
      if address.private? || address.loopback? || address.link_local?
        Rails.logger.error "Activation: request from a private address #{address}, so it counts toward no network (#205)"
        nil
      else
        (address.ipv6? ? address.mask(56) : address).to_s
      end
    end
  end

  # INFO: fc 18oct26 SQLite transactions begin IMMEDIATE, so maximum and insert run under one write lock: serials stay dense
  def self.next_serial
    serial = (maximum(:serial) || 0) + 1
    if SERIALS.cover?(serial)
      serial
    else
      raise Activation::Refusal.new(:no_serial)
    end
  end

  def bind(public_key, **seen)
    key = keys.find_or_initialize_by(public_key:)
    renewed = key.persisted?
    if key.revoked?
      key.update!(seen)
    else
      key.update!(status: bound_to?(key) ? :active : :pending, **seen)
    end
    key.touch unless key.saved_changes?
    forget_old_pending_keys if key.pending?
    Rails.event.notify("unit_key.bound", unit_id: id, device: slug, status: key.status, release: key.release, version: key.version, renewed:)
    key
  end

  # INFO: fc 14nov26 a row per stretch on one firmware, so a unit that goes back to a version gets a new row
  def attest_firmware(version:, release:)
    firmware = firmwares.newest_first.first
    if firmware&.version == version && firmware.release == release
      firmware.touch
    else
      firmware = firmwares.create!(version:, release:)
    end
    Rails.event.notify "unit.firmware_attested", unit_id: id, device: slug, version:, release:, changed: firmware.previously_new_record?
    firmware
  end

  # INFO: fc 14nov26 a registration signs the version too, so a unit that has not attested since shows the one it registered with
  def signed_firmware = latest_firmware || active_key

  def reported_firmware = Unit::ReportedFirmware.new(self).stretches

  def address = format("%02X%04X", MANUFACTURER_PREFIX, serial)

  def slug = "go-#{address.downcase}"

  private
    def bound_to?(key)
      active = keys.active.first
      active.nil? || active == key
    end

    # INFO: fc 09nov26 anyone can sign a fresh key for a known DEVICEID, so only the newest pending keys are kept for an admin
    def forget_old_pending_keys
      keys.pending.order(updated_at: :desc, id: :desc).offset(PENDING_KEYS_KEPT).destroy_all
    end
end
