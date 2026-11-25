require "test_helper"

class Briefing::SimulatorBuildsControllerTest < ActionDispatch::IntegrationTest
  test "the simulator page does not call GitHub before its frame loads" do
    get "/try"

    assert_response :success
    assert_select "turbo-frame#simulator_build[src]"
  end

  test "the frame offers the simulator of the newest stable release that holds it" do
    VCR.use_cassette("github/simulator") do
      with_config(simulator_build: nil) { get "/briefing/simulator_build" }
    end

    assert_response :success
    assert_select "turbo-frame#simulator_build [role=status]", count: 0
    assert_match "v0.8.1", response.body
  end

  test "the French simulator frame labels the Bravo control in French" do
    VCR.use_cassette("github/simulator") do
      with_config(simulator_build: nil) { get "/briefing/simulator_build", params: { locale: :fr } }
    end

    assert_response :success
    assert_select "label[data-simulator-target=bravoControl]", text: "Avec Bravo (skyBlip Tango)" do
      assert_select "input[type=checkbox][data-action='change->simulator#restart']"
    end
  end

  test "GitHub failure is contained in the simulator frame" do
    VCR.use_cassette("github/unavailable") do
      with_config(simulator_build: nil) { get "/briefing/simulator_build" }
    end

    assert_response :success
    assert_select "turbo-frame#simulator_build [role=status]", text: /could not be loaded/
  end
end
