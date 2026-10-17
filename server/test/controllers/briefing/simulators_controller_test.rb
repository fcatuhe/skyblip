require "test_helper"

class Briefing::SimulatorsControllerTest < ActionDispatch::IntegrationTest
  # A lazy frame waits for the viewport, and the workbench stayed on its loading line in a real browser.
  test "the workbench loads firmware right after the page, without querying releases in the page request" do
    get briefing_simulator_path

    assert_response :success
    assert_select "turbo-frame#simulator_build[src='#{briefing_simulator_build_path(mode: :workbench)}'][loading=eager]", count: 1
    assert_select "turbo-frame#simulator_build [role=status]", text: I18n.t("simulator_builds.loading")
    assert_select "main script, main style, main [style]", count: 0
  end

  test "the localized workbench loads its localized build frame" do
    get briefing_simulator_path(locale: :fr)

    assert_response :success
    assert_select "turbo-frame#simulator_build[src='#{briefing_simulator_build_path(locale: :fr, mode: :workbench)}']", count: 1
    assert_select "turbo-frame#simulator_build [role=status]", text: I18n.t("simulator_builds.loading", locale: :fr)
  end
end
