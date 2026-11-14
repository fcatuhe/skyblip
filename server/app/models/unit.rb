class Unit < ApplicationRecord
  MANUFACTURER_PREFIX = 0x00
  # INFO: fc 18oct26 serial 0 would put 0x000000 on the air, which decoders read as no address
  SERIALS = 1..0xFFFF
  NEW_PER_DAY = 50
  NEW_PER_NETWORK_PER_DAY = 5
  PENDING_KEYS_KEPT = 5

  has_many :keys, dependent: :destroy
  has_one :active_key, -> { active }, class_name: "Unit::Key"

  validates :device_id, uniqueness: true, format: { with: Activation::DEVICE_ID }
  validates :serial, uniqueness: true, inclusion: { in: SERIALS }

  scope :enrolled_today, -> { where(created_at: Time.current.all_day) }

  def self.register(device_id:, public_key:, **seen)
    transaction do
      unit = find_by(device_id:) || enroll(device_id, network_of(seen[:ip_address]))
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
    raise Activation::Refusal.new(:network_cap) if network_cap_reached?(network)

    unit = create!(device_id:, serial: next_serial, enrolled_from: network)
    Rails.event.notify "unit.enrolled", unit_id: unit.id, serial: unit.serial, device: unit.slug
    Rails.logger.error "Activation: #{NEW_PER_DAY} new units today, the daily cap, so new ones wait for midnight UTC" if daily_cap_reached?
    unit
  end

  def self.network_cap_reached?(network) = network.present? && enrolled_today.where(enrolled_from: network).count >= NEW_PER_NETWORK_PER_DAY

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
