class Unit < ApplicationRecord
  include Activating, Reporting

  MANUFACTURER_PREFIX = 0x00
  BLUETOOTH_TAGS = 0..0xFFFF
  PENDING_KEYS_KEPT = 5

  has_many :keys, dependent: :destroy
  has_one :active_key, -> { active }, class_name: "Unit::Key"
  has_many :firmwares, dependent: :destroy
  has_one :latest_firmware, -> { newest_first }, class_name: "Unit::Firmware"

  enum :product, %w[go bravo].index_by(&:itself), validate: { allow_nil: true }
  enum :variant, %w[standard plus].index_by(&:itself), validate: { allow_nil: true }

  validates :device_id, uniqueness: true, format: { with: Activation::DEVICE_ID }
  validates :bluetooth_tag, inclusion: { in: BLUETOOTH_TAGS }, allow_nil: true

  scope :newest_seen_first, -> { order(updated_at: :desc, id: :desc) }

  def self.with_slug(slug)
    if (serial = slug.to_s[/\Ago-#{format("%02x", MANUFACTURER_PREFIX)}(\h{4})\z/, 1])
      where(serial: serial.hex)
    elsif (tag = slug.to_s[/\Aactivate-(\h{4})\z/, 1])
      go.where(serial: nil, bluetooth_tag: tag.hex)
    elsif (tag = slug.to_s[/\Abravo-(\h{4})\z/, 1])
      bravo.where(bluetooth_tag: tag.hex)
    else
      none
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
    touch
    forget_old_pending_keys if key.pending?
    Rails.event.notify("unit_key.bound", unit_id: id, device: slug, status: key.status, release: key.release, version: key.version, renewed:)
    key
  end

  # INFO: fc 25nov26 a registration of v1, from firmware as old as v0.8.0, signs no product nor variant, so it names neither
  def attest_firmware(version:, release:, product: nil, variant: nil, **seen)
    firmware = transaction do
      if product
        identify(product:, variant:, version:, source: "attested", **seen)
      else
        touch
      end
      record_firmware(version:, release:, variant:, source: "attested")
    end
    Rails.event.notify "unit.firmware_attested", unit_id: id, device: slug, version:, release:, variant:, changed: firmware.previously_new_record?
    firmware
  end

  def identify(product:, variant:, version:, source:, **seen)
    before = self.variant
    update!({ updated_at: Time.current, product:, variant: }.merge(seen))
    if before && before != variant
      Rails.event.notify "unit.variant_changed", unit_id: id, device: slug, from: before, to: variant, version:, source:
    end
  end

  # INFO: fc 14nov26 a row per stretch on one firmware and one variant, so a unit that goes back to a version gets a new row
  def record_firmware(version:, release:, source:, variant: nil, at: Time.current)
    latest = firmwares.newest_first.first
    if latest&.continued_by?(version:, release:, source:, variant:)
      latest.continue(source:, variant:, at:)
    else
      firmwares.create!(version:, release:, variant:, source:, created_at: at, updated_at: at)
    end
  end

  def address = (format("%02X%04X", MANUFACTURER_PREFIX, serial) if activated?)

  def slug
    if activated?
      "go-#{address.downcase}"
    elsif bluetooth_tag
      "#{bravo? ? "bravo" : "activate"}-#{format("%04x", bluetooth_tag)}"
    end
  end

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
