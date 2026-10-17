require "test_helper"

class SimulatorHelperTest < ActionView::TestCase
  include SimulatorHelper
  include Rails.application.routes.url_helpers

  test "runtime firmware sources use the same-origin release proxy" do
    assert_equal "/releases/v0.2.0/embed.js", simulator_module_path(tag: "v0.2.0")
    assert_match %r{\A/assets/skyblip/update-[a-f0-9]+\.js\z}, ApplicationController.helpers.update_module_path
  end

  test "browser and node share app-owned protocol module names" do
    imports = JSON.parse(Rails.application.importmap.to_json(resolver: ApplicationController.helpers)).fetch("imports")

    %w[ble cbor dev_simulator image smp update].each do |name|
      assert_match %r{\A/assets/skyblip/#{name}-[a-f0-9]+\.js\z}, imports.fetch("#skyblip/#{name}")
    end
  end

  test "the workbench receives the scenario bundle from the same release as its runtime" do
    @release = Release.new("tag_name" => "dev-1005", "name" => "Development", "prerelease" => true,
                           "published_at" => "2026-10-17T00:00:00Z", "assets" => [])
    @workbench = true

    render template: "briefing/simulator_builds/show"

    assert_select "[data-dev-simulator-src-value='/releases/dev-1005/embed.js']"
    assert_select "[data-dev-simulator-manifest-value='/releases/dev-1005/scenarios.json']"
  end

  test "the workbench keeps own-ship sensors editable without inline behavior" do
    render partial: "shared/simulator_dev", locals: { module_path: "/releases/v0.2.0/embed.js" }

    %w[spd alt trk trn clb slp sats air bat rng].each do |id|
      assert_select "input##{id}[type=range][data-action='input->dev-simulator#control']", count: 1
    end
    assert_select "[data-dev-simulator-src-value='/releases/v0.2.0/embed.js']", count: 1
    assert_select "script, style, [style], [onclick], [oninput], [onpointerdown]", count: 0
  end
end
