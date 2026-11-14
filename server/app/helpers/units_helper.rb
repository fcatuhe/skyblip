module UnitsHelper
  UNITS_LISTED = 10

  def unit_build(record)
    case record&.release
    when true then "release"
    when false then "development"
    end
  end

  def unit_links(units)
    links = units.first(UNITS_LISTED).map { |unit| link_to unit.address, admin_unit_path(unit), class: "mono" }
    more = units.size - UNITS_LISTED
    if more > 0
      safe_join(links, ", ") + ", and #{more} more"
    else
      safe_join(links, ", ")
    end
  end
end
