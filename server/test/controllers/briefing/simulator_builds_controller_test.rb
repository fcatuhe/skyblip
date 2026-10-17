require "test_helper"

class Briefing::SimulatorBuildsControllerTest < ActionDispatch::IntegrationTest
  test "the simulator page does not call GitHub before its frame loads" do
    get "/try"

    assert_response :success
    assert_select "turbo-frame#simulator_build[src]"
  end

  test "releases without simulator assets show an explicit empty state" do
    VCR.use_cassette("github/releases") do
      get "/briefing/simulator_build"
    end

    assert_response :success
    assert_select "turbo-frame#simulator_build [role=status]", text: /No simulator build/
  end

  test "GitHub failure is contained in the simulator frame" do
    VCR.use_cassette("github/unavailable") do
      get "/briefing/simulator_build"
    end

    assert_response :success
    assert_select "turbo-frame#simulator_build [role=status]", text: /could not be loaded/
  end
end
