module UnitsHelper
  UNITS_LISTED = 10
  FIRMWARE_SOURCES = { "read" => "reported by a browser", "installed" => "installed from the briefing app", "attested" => "signed by the unit" }.freeze

  def unit_build(record)
    case record&.release
    when true then "release"
    when false then "development"
    end
  end

  def unit_label(unit) = unit.address || unit.slug || unit.device_id

  def unit_links(units)
    links = units.first(UNITS_LISTED).map { |unit| link_to unit_label(unit), admin_unit_path(unit), class: "mono" }
    more = units.size - UNITS_LISTED
    if more > 0
      safe_join(links, ", ") + ", and #{more} more"
    else
      safe_join(links, ", ")
    end
  end

  # INFO: fc 14nov26 a registration signs the version too, so a unit with no firmware recorded since shows the one it registered with
  def unit_firmware(unit) = unit.latest_firmware || unit.active_key

  def firmware_source(record)
    case record
    when Unit::Firmware then FIRMWARE_SOURCES.fetch(record.source)
    when Unit::Key then "registered with"
    end
  end
end
