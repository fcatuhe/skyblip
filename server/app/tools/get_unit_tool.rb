class GetUnitTool < ApplicationTool
  tool_name "get_unit"
  title "Read a unit's record"
  description "What the server holds on a skyBlip unit, any product, from its first connection to the briefing app: its product " \
              "and variant, its DEVICEID and Bluetooth tag, and once activated its serial, its ADS-L address under AMT entry 58 " \
              "and each key it registered with, active, pending or revoked, and the visitor tokens get_visitor reads for the " \
              "browsers that activated it. Then its firmware, newest first, one stretch per version, build and variant, each " \
              "with its source: read, a browser's report nobody vouches for, installed, an install from the briefing app the " \
              "unit came back on, or attested, signed by the unit. A tag names up to 65536 units, so a slug from one can match " \
              "several, so the answer is always a list. For admins only."
  input_schema properties: { device: DEVICE }, required: [ "device" ]
  annotations(**READING)

  def self.call(server_context:, device:)
    if admin(server_context)
      units = Unit.with_slug(device).includes(:keys, :firmwares).to_a
      if units.any?
        json(units.map { facts_of(it) })
      else
        refused "No unit is known as #{device.to_s.truncate(40)}"
      end
    else
      admins_only
    end
  end

  def self.facts_of(unit)
    { device: unit.slug, product: unit.product, variant: unit.variant, device_id: unit.device_id,
      bluetooth_tag: unit.bluetooth_tag && format("%04X", unit.bluetooth_tag), serial: unit.serial, address: unit.address,
      first_seen_at: unit.created_at.utc.iso8601, last_seen_at: unit.updated_at.utc.iso8601, activated_at: unit.activated_at&.utc&.iso8601,
      activated_by: Ahoy::Visit.activating(unit).distinct.pluck(:visitor_token),
      keys: unit.keys.sort_by(&:created_at).map do |key|
        { status: key.status, version: key.version, release: key.release, public_key_starts: key.public_key.first(16),
          first_registered_at: key.created_at.utc.iso8601, last_registered_at: key.updated_at.utc.iso8601 }
      end,
      firmware: unit.firmwares.sort_by { [ it.created_at, it.id ] }.reverse.map do |firmware|
        { version: firmware.version, release: firmware.release, variant: firmware.variant, source: firmware.source,
          installed_from: firmware.from_version, install_lasted_ms: firmware.lasted_ms,
          first_seen_at: firmware.created_at.utc.iso8601, last_seen_at: firmware.updated_at.utc.iso8601 }
      end }
  end
  private_class_method :facts_of
end
