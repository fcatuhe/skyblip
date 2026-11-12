require "application_system_test_case"

class BriefingTest < ApplicationSystemTestCase
  test "the workbench says so when no release carries the simulator" do
    VCR.use_cassette("github/releases") do
      visit "/briefing/simulator"

      assert_text "No simulator build has been published yet."
    end
  end

  test "the developer tools sit behind the menu, the console among them" do
    visit "/briefing"
    find(".app-menu summary").click
    within(".app-menu") { click_link "Developer tools" }
    screen "40-tools"
    click_link "Bluetooth console"

    assert_button "Connect", disabled: true
    assert_current_path "/briefing/console"
  end
end
