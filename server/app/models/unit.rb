class Unit < ApplicationRecord
  MANUFACTURER_PREFIX = 0x00
  SERIALS = 0..0xFFFF
  NEW_PER_DAY = 50

  has_many :keys, dependent: :destroy
  has_one :active_key, -> { active }, class_name: "Unit::Key"

  validates :device_id, uniqueness: true, format: { with: Activation::DEVICE_ID }
  validates :serial, uniqueness: true, inclusion: { in: SERIALS }

  scope :enrolled_today, -> { where(created_at: Time.current.all_day) }

  def self.register(device_id:, public_key:, **seen)
    transaction do
      unit = find_by(device_id:) || enroll(device_id)
      unit.bind(public_key, **seen)
    end
  end

  def self.daily_cap_reached? = enrolled_today.count >= NEW_PER_DAY

  def self.enroll(device_id)
    if daily_cap_reached?
      Rails.logger.error "Activation: the daily cap of #{NEW_PER_DAY} new units is reached, #{device_id} refused"
      raise Activation::Refusal.new(:daily_cap)
    end

    create!(device_id:, serial: next_serial)
  end

  def self.next_serial
    serial = (maximum(:serial) || -1) + 1
    if SERIALS.cover?(serial)
      serial
    else
      raise Activation::Refusal.new(:no_serial)
    end
  end

  def bind(public_key, **seen)
    key = keys.find_or_initialize_by(public_key:)
    key.update!(status: bound_to?(key) ? :active : :pending, **seen)
    key
  end

  def address = format("%02X%04X", MANUFACTURER_PREFIX, serial)

  private
    def bound_to?(key)
      active = keys.active.first
      active.nil? || active == key
    end
end
