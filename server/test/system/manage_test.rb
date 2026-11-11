require "application_system_test_case"

class ManageTest < ApplicationSystemTestCase
  setup do
    script = Rails.root.join("test/system/fake_skyblip.js").read
    page.driver.browser.page.command("Page.addScriptToEvaluateOnNewDocument", source: script)
  end

  test "the update page shows the settings a unit holds by name, and saves only what changed" do
    on_the_shelf do
      visit "/manage"
      connect_go
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

  test "the own ICAO is edited in hex beside the other settings" do
    on_the_shelf do
      visit "/manage"
      connect_go
      assert_field "Own ICAO address", with: ""
      fill_in "Own ICAO address", with: "3c6586"
      click_button "Save"
      assert_text "Allow the change"
      assert_equal({ "own_icao" => 0x3C6586 }, page.evaluate_script("fakeSkyblip.staged"))
      page.execute_script("fakeSkyblip.press()")
      assert_field "Own ICAO address", with: "3C6586"
    end
  end

  test "an own ICAO left half typed for one unit does not block the next unit, which holds none" do
    on_the_shelf do
      visit "/manage"
      connect_go
      fill_in "Own ICAO address", with: "3C65"
      click_button "Disconnect"
      assert_button "Connect"

      page.execute_script("delete fakeSkyblip.stored.own_icao")
      click_button "Connect"
      assert_field "Callsign", with: "F-CABC"
      assert_no_field "Own ICAO address"
      fill_in "Callsign", with: "F-JXYZ"
      click_button "Save"
      assert_text "Allow the change"
      assert_equal({ "callsign" => "F-JXYZ" }, page.evaluate_script("fakeSkyblip.staged"))
    end
  end

  test "a firmware that holds no own ICAO is offered none, and its other settings still save" do
    on_the_shelf do
      visit "/manage"
      page.execute_script("delete fakeSkyblip.stored.own_icao")
      connect_go
      assert_field "Callsign", with: "F-CABC"
      assert_no_field "Own ICAO address"
      fill_in "Callsign", with: "F-JXYZ"
      click_button "Save"
      assert_equal({ "callsign" => "F-JXYZ" }, page.evaluate_script("fakeSkyblip.staged"))
    end
  end

  test "a Go is offered the Go images of its track, and a Bravo none of them" do
    on_the_shelf do
      visit "/manage"
      connect_go
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
    on_the_shelf do
      visit "/manage"
      page.execute_script("fakeSkyblip.address = null")
      connect_go
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

  test "a Go starts on the newest slim image of its track" do
    on_the_shelf do
      visit "/manage"
      connect_go
      assert_equal "/releases/dev-1005/skyblip-go-dev-1005.signed.bin", find("[data-manage-target=shelf]").value
    end
  end

  test "a save refused on the glass is said under Save, and the connection asks nothing more" do
    on_the_shelf do
      visit "/manage"
      connect_go
      select "Loud", from: "Alarm volume"
      click_button "Save"
      assert_text "Allow the change"
      page.execute_script("fakeSkyblip.refuse()")
      within ".manage-settings" do
        assert_selector "[role=status]", text: "The device said no"
      end
      assert_selector "[data-manage-target=notes] p", text: "One press on the device refused a question"
      assert_button "Save", disabled: true
    end
  end

  private
    def on_the_shelf(&)
      VCR.use_cassette("github/releases") { VCR.use_cassette("github/firmware", &) }
    end

    def connect_go
      click_button "Connect"
      assert_selector "[data-manage-target=chosen]", text: /\d/
    end
end
