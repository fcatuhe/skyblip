require "test_helper"

class Briefing::SimulatorBuildsControllerTest < ActionDispatch::IntegrationTest
  test "the simulator page does not call GitHub before its frame loads" do
    get "/try"

    assert_response :success
    assert_select "turbo-frame#simulator_build[src]"
  end

  test "the frame offers the simulator of the newest main build that holds it" do
    VCR.use_cassette("github/releases") do
      get "/briefing/simulator_build"
    end

    assert_response :success
    assert_select "turbo-frame#simulator_build [role=status]", count: 0
    assert_match "dev-1765", response.body
  end

  test "GitHub failure is contained in the simulator frame" do
    VCR.use_cassette("github/unavailable") do
      get "/briefing/simulator_build"
    end

    assert_response :success
    assert_select "turbo-frame#simulator_build [role=status]", text: /could not be loaded/
  end
end
