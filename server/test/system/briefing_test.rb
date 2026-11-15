require "application_system_test_case"

class BriefingTest < ApplicationSystemTestCase
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
