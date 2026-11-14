class GetUnitTool < ApplicationTool
  tool_name "get_unit"
  title "Read a unit's record"
  description "What the server holds on an activated skyBlip Go: its serial, address, when it was activated, each key " \
              "it registered with, active, pending or revoked, the firmware it registered from and whether it is a " \
              "release build, the firmware the unit signed with its active key, newest first, and the firmware " \
              "browsers reported it running, which nobody vouches for. A unit is known only once activated. For admins only."
  input_schema properties: { device: DEVICE }, required: [ "device" ]
  annotations(**READING)

  def self.call(server_context:, device:)
    if admin(server_context)
      if (unit = Unit.find_by_slug(device))
        json facts_of(unit)
      else
        refused "No activated unit is known as #{device.to_s.truncate(40)}"
      end
    else
      admins_only
    end
  end

  def self.facts_of(unit)
    { device: unit.slug, serial: unit.serial, address: unit.address, activated_at: unit.created_at.utc.iso8601,
      keys: unit.keys.order(:created_at).map do |key|
        { status: key.status, version: key.version, release: key.release, public_key_starts: key.public_key.first(16),
          first_registered_at: key.created_at.utc.iso8601, last_registered_at: key.updated_at.utc.iso8601 }
      end,
      signed_firmware: unit.firmwares.newest_first.map do |firmware|
        { version: firmware.version, release: firmware.release,
          first_signed_at: firmware.created_at.utc.iso8601, last_signed_at: firmware.updated_at.utc.iso8601 }
      end,
      reported_firmware: unit.reported_firmware.map do |stretch|
        { version: stretch.version, track: stretch.track,
          first_seen_at: stretch.first_seen_at.utc.iso8601, last_seen_at: stretch.last_seen_at.utc.iso8601 }
      end }
  end
  private_class_method :facts_of
end
