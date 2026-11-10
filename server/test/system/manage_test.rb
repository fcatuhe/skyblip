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

  test "a Go is offered the Go images of its track, and a Bravo none of them" do
    VCR.use_cassette("github/releases") do
      visit "/manage"
      click_button "Connect"
      assert_selector "[data-manage-target=track]", text: "development"
      assert_selector "option[data-product=skyblip_go]:not([hidden])", visible: :all, minimum: 1
    end

    VCR.use_cassette("github/releases") do
      visit "/manage"
      page.execute_script("fakeSkyblip.product = 'skyblip_bravo'")
      click_button "Connect"
      assert_selector "[data-manage-target=track]", text: "development"
      assert_selector "option[data-product=skyblip_go]", visible: :all, minimum: 1
      assert_no_selector "option[data-product]:not([hidden])", visible: :all
    end
  end

  test "a Go without an address is not activated, and a Bravo, which has none, is never said to be" do
    VCR.use_cassette("github/releases") do
      visit "/manage"
      page.execute_script("fakeSkyblip.address = null")
      click_button "Connect"
      assert_selector "[data-manage-target=notes] p", text: "This unit is not activated yet"
    end

    VCR.use_cassette("github/releases") do
      visit "/manage"
      page.execute_script("Object.assign(fakeSkyblip, { address: null, product: 'skyblip_bravo' })")
      click_button "Connect"
      assert_select "Units", selected: "Metric: km and km/h"
      assert_selector "[data-manage-target=track]", text: "development"
      assert_no_selector "[data-manage-target=notes] p", text: "This unit is not activated yet"
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
