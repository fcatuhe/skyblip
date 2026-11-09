class Unit < ApplicationRecord
  MANUFACTURER_PREFIX = 0x00
  # INFO: fc 18oct26 serial 0 would put 0x000000 on the air, which decoders read as no address
  SERIALS = 1..0xFFFF
  NEW_PER_DAY = 50
  NEW_PER_NETWORK_PER_DAY = 5

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

  def self.daily_cap_reached? = enrolled_today.count >= NEW_PER_DAY

  def self.enroll(device_id, network)
    raise Activation::Refusal.new(:daily_cap) if daily_cap_reached?
    raise Activation::Refusal.new(:network_cap) if network_cap_reached?(network)

    unit = create!(device_id:, serial: next_serial, enrolled_from: network)
    Rails.logger.error "Activation: #{NEW_PER_DAY} new units today, the daily cap, so new ones wait for midnight UTC" if daily_cap_reached?
    unit
  end

  def self.network_cap_reached?(network) = network.present? && enrolled_today.where(enrolled_from: network).count >= NEW_PER_NETWORK_PER_DAY

  # INFO: fc 08nov26 one IPv6 subscriber holds a whole /64, so a network is that /64, or one IPv4 address
  def self.network_of(ip_address)
    if ip_address.present?
      address = IPAddr.new(ip_address)
      address = address.native if address.ipv4_mapped?
      if address.private? || address.loopback? || address.link_local?
        Rails.logger.error "Activation: request from a private address #{address}, so it counts toward no network (#205)"
        nil
      else
        (address.ipv6? ? address.mask(64) : address).to_s
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
    if key.revoked?
      key.update!(seen)
    else
      key.update!(status: bound_to?(key) ? :active : :pending, **seen)
    end
    key.touch unless key.saved_changes?
    key
  end

  def address = format("%02X%04X", MANUFACTURER_PREFIX, serial)

  private
    def bound_to?(key)
      active = keys.active.first
      active.nil? || active == key
    end
end
