require "test_helper"

class Ownship::AircraftTypeTest < ActiveSupport::TestCase
  # The settings forms send these codes as the unit's aircraft_type, so they must be the device menu's.
  test "the codes follow the aircraft types of the device menu" do
    menu = Rails.root.join("../firmware/products/skyblip_go/pages/menu.h").read[/kAircraftTypes\[\] = \{(.*?)\};/m, 1].scan(/\{(\d+), "([^"]+)"\}/)

    assert_equal menu.map { |code, _| code.to_i }, Ownship::AircraftType.all.map(&:code)
    assert_equal menu.map(&:last).map { it.split.first }, Ownship::AircraftType::KEYS.map { it.split("_").first.upcase }
    assert_equal 4, Ownship::AircraftType.find("glider").code
    assert_equal 15, Ownship::AircraftType.find("heavy_helicopter").code
    assert_equal [ 3, 16, 17 ], %w[light_helicopter hang_glider paramotor].map { Ownship::AircraftType.find(it).code }
  end

  test "a type with no drawing shows the light aircraft's" do
    helicopter = Ownship::AircraftType.find("light_helicopter")

    assert_not helicopter.drawn?
    assert_equal Ownship::AircraftType.default.ownship, helicopter.ownship
    assert_equal Ownship::AircraftType.default.planforms.map(&:key), helicopter.planforms.map(&:key)
  end

  test "the light aircraft offers the Ménestrel, the Spitfire and skyShip's original outline, the Ménestrel first" do
    light = Ownship::AircraftType.default

    assert_equal %w[menestrel spitfire skyship_reference], light.planforms.map(&:key)
    assert_equal [ 7.8, 11.23 ], light.planforms.first(2).map { it.span.round(2) }
  end

  test "an unknown key is the default type" do
    assert_equal Ownship::AircraftType.default, Ownship::AircraftType.find("zeppelin")
    assert_equal Ownship::AircraftType.default, Ownship::AircraftType.find(nil)
  end
end
