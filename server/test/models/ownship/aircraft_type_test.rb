require "test_helper"

class Ownship::AircraftTypeTest < ActiveSupport::TestCase
  test "the codes follow the settings select on the manage page" do
    english = Rails.root.join("content/pages/manage.html.erb").read[/aircraft_types: \[(.*?)\]/m, 1].scan(/"([^"]+)"/).flatten

    assert_equal english, Ownship::AircraftType.all.map(&:name)
    assert_equal 4, Ownship::AircraftType.find("glider").code
  end

  test "a type with no drawing shows the light aircraft's" do
    helicopter = Ownship::AircraftType.find("helicopter")

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
