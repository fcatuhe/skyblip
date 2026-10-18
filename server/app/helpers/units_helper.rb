module UnitsHelper
  def unit_key_build(key)
    case key&.release
    when true then "release"
    when false then "development"
    end
  end
end
