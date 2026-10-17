require "application_system_test_case"

class BriefingTest < ApplicationSystemTestCase
  test "the update page offers the published firmware once its frame loads" do
    VCR.use_cassette("github/releases") do
      visit "/manage"

      assert_selector "turbo-frame#firmware_shelf[complete]"
      assert_selector "option", text: "skyBlip dev 0.1.0+1005, Full image", visible: :all
      assert_field type: "file"
    end
  end

  test "the update page keeps the file picker when GitHub is unavailable" do
    VCR.use_cassette("github/unavailable") do
      visit "/manage"

      assert_text "GitHub's release list is unavailable"
      assert_field type: "file"
      assert_no_selector "select[data-manage-target=shelf]"
    end
  end

  test "the workbench says so when no release carries the simulator" do
    VCR.use_cassette("github/releases") do
      visit "/briefing/simulator"

      assert_text "No simulator build has been published yet."
    end
  end

  test "briefing leads to the console" do
    visit "/briefing"
    click_on "Open the Bluetooth console"

    assert_button "Connect", disabled: true
    assert_current_path "/briefing/console"
  end
end
