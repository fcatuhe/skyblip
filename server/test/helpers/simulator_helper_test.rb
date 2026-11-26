require "test_helper"

class SimulatorHelperTest < ActionView::TestCase
  include SimulatorHelper
  include Rails.application.routes.url_helpers

  test "runtime firmware sources use the same-origin release proxy" do
    assert_equal "/releases/v0.2.0/skyblip-simulator-embed.js", simulator_module_path("v0.2.0")
  end

  # The altimeter's 9 sat at 330.6 deg while its needle pointed 324 deg at 900 ft, as one turn was laid over 980 ft.
  test "every numeral of a needle dial sits where the needle points at its value" do
    %w[speed altitude climb].each do |axis|
      dial = dial(axis)
      gauge_numerals(dial).each do |text, x, y|
        value = Integer(text) * dial[:scale].fetch(:divisor, 1)
        values = axis == "climb" && value.positive? ? [ value, -value ] : [ value ]
        assert values.any? { |each| apart(law_position(gauge_law(dial, :needle), each), angle_of(x, y)) < 0.05 },
               "#{axis} #{text} drawn at #{angle_of(x, y)}"
      end
    end
  end

  test "the track card brings each numeral under the index at its heading" do
    dial = dial("track")
    gauge_numerals(dial).each do |text, x, y|
      heading = SimulatorHelper::GAUGE_CARDINALS.key(text) || Integer(text) * 10
      assert_operator apart(angle_of(x, y) + law_position(gauge_law(dial, :card), heading), 0), :<, 0.05, text
    end
  end

  test "the turn plane reaches the rate mark at a standard rate turn, and the ball stays in its tube" do
    turn = dial("turn")

    assert_equal turn[:rate][:mark_deg], law_position(gauge_law(turn, :plane), turn[:rate][:standard_dps_e1])
    ball = gauge_law(turn, :ball)
    assert_operator ball[:points].last.last + SimulatorHelper::BALL_RADIUS, :<=, SimulatorHelper::BALL_TUBE_HALF_WIDTH
  end

  test "browser and node share app-owned protocol module names" do
    imports = JSON.parse(Rails.application.importmap.to_json(resolver: ApplicationController.helpers)).fetch("imports")

    %w[ble cbor fleet image simulated_traffic smp update].each do |name|
      assert_match %r{\A/assets/skyblip/#{name}-[a-f0-9]+\.js\z}, imports.fetch("#skyblip/#{name}")
    end
  end

  test "the workbench receives the scenario bundle from the same release as its runtime" do
    @release = Release.new("tag_name" => "dev-1005", "name" => "Development", "prerelease" => true,
                           "created_at" => "2026-10-17T00:00:00Z", "assets" => [], "body" => nil)
    @tag = @release.tag
    @workbench = true

    render template: "briefing/simulator_builds/show"

    assert_select "[data-dev-simulator-src-value='/releases/dev-1005/skyblip-simulator-embed.js']"
    assert_select "[data-dev-simulator-manifest-value='/releases/dev-1005/skyblip-simulator-scenarios.json']"
  end

  test "the workbench keeps own-ship sensors editable without inline behavior" do
    render partial: "shared/simulator_dev", locals: { module_path: "/releases/v0.2.0/skyblip-simulator-embed.js" }

    %w[speed altitude track turn climb slip satellites airmass battery range].each do |axis|
      assert_select "input[type=range][data-dev-simulator-target=#{axis}][data-dev-simulator-axis-param=#{axis}][data-action='dev-simulator#steer']", count: 1
      assert_select "output[data-dev-simulator-target=#{axis}Readout]", count: 1
    end
    assert_select "[data-dev-simulator-src-value='/releases/v0.2.0/skyblip-simulator-embed.js']", count: 1
    assert_select "script, style, [style], [onclick], [oninput], [onpointerdown]", count: 0
  end

  private
    def dial(axis) = SimulatorHelper::SIMULATOR_DIALS.find { it[:axis] == axis }
    def angle_of(x, y) = (Math.atan2(x, -y) * 180 / Math::PI) % 360
    def apart(a, b) = ((a - b) % 360).then { [ it, 360 - it ].min }

    def law_position(law, value)
      at = law[:cycle] ? value % law[:cycle] : value
      (from_value, from_position), (to_value, to_position) = law[:points].each_cons(2).find { |(low, _), (high, _)| at.between?(low, high) }
      from_position + (at - from_value) * (to_position - from_position).fdiv(to_value - from_value)
    end
end
