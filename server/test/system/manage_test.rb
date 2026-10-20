require "application_system_test_case"

class ManageTest < ApplicationSystemTestCase
  setup do
    script = Rails.root.join("test/system/fake_skyblip.js").read
    page.driver.browser.page.command("Page.addScriptToEvaluateOnNewDocument", source: script)
  end

  test "the update page shows the settings a unit holds by name, and saves only what changed" do
    VCR.use_cassette("github/releases") do
      visit "/manage"
      click_button "Connect"
      assert_select "Units", selected: "Metric: km and km/h"
      assert_select "Alarm volume", selected: "Soft"
      assert_text "Default: Nautical: NM and kt"

      select "Loud", from: "Alarm volume"
      click_button "Save"
      assert_text "Allow the change"
      assert_equal({ "alarm_volume" => "loud" }, page.evaluate_script("fakeSkyblip.staged"))

      page.execute_script("fakeSkyblip.press()")
      assert_selector "[role=status]", text: "The device stored the settings"
      assert_select "Alarm volume", selected: "Loud"
    end
  end

  test "a phone that kept the smallest Bluetooth packet is told so, and what to do" do
    VCR.use_cassette("github/releases") do
      visit "/manage"
      page.execute_script("fakeSkyblip.mtu = 23")
      click_button "Connect"
      assert_selector "[role=status]", text: "smallest packet size, 20 bytes. Disconnect and connect again"
    end
  end
end
