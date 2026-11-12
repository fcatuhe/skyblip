require "application_system_test_case"

class ConfigureTest < ApplicationSystemTestCase
  setup do
    script = Rails.root.join("test/system/fake_skyblip.js").read
    page.driver.browser.page.command("Page.addScriptToEvaluateOnNewDocument", source: script)
  end

  test "the settings page shows the settings a unit holds by name, and saves only what changed" do
    visit "/configure"
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

  test "the own ICAO is edited in hex beside the other settings" do
    visit "/configure"
    connect_go
    assert_field "Own ICAO address", with: ""
    fill_in "Own ICAO address", with: "3c6586"
    click_button "Save"
    assert_text "Allow the change"
    assert_equal({ "own_icao" => 0x3C6586 }, page.evaluate_script("fakeSkyblip.staged"))
    page.execute_script("fakeSkyblip.press()")
    assert_field "Own ICAO address", with: "3C6586"
  end

  test "an own ICAO left half typed for one unit does not block the next unit, which holds none" do
    visit "/configure"
    connect_go
    fill_in "Own ICAO address", with: "3C65"
    within(".manage") { click_button "Disconnect" }
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

  test "a firmware that holds no own ICAO is offered none, and its other settings still save" do
    visit "/configure"
    page.execute_script("delete fakeSkyblip.stored.own_icao")
    connect_go
    assert_no_field "Own ICAO address"
    fill_in "Callsign", with: "F-JXYZ"
    click_button "Save"
    assert_equal({ "callsign" => "F-JXYZ" }, page.evaluate_script("fakeSkyblip.staged"))
  end

  test "a save refused on the glass is said under Save, and the connection asks nothing more" do
    visit "/configure"
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

  test "a Bravo, which has no address, is never said to be not activated" do
    visit "/configure"
    page.execute_script("Object.assign(fakeSkyblip, { address: null, product: 'skyblip_bravo' })")
    connect_go
    assert_select "Units", selected: "Metric: km and km/h"
    assert_no_selector "[data-manage-target=notes] p", text: "This unit is not activated yet"
  end

  test "a Bravo offers its capture, armed with two presses on the unit, and a Go does not" do
    visit "/configure"
    connect_go
    assert_select "Units"
    assert_no_button "Arm"
    within(".manage") { click_button "Disconnect" }

    page.execute_script("Object.assign(fakeSkyblip, { address: null, product: 'skyblip_bravo' })")
    click_button "Connect"
    select "Power run: power and duty", from: "Capture"
    click_button "Arm"
    assert_equal "power_run", page.evaluate_script("fakeSkyblip.capture")
    page.execute_script("fakeSkyblip.press()")
    assert_selector "[role=status]", text: "Bravo records the capture you chose"

    click_button "Stop the capture"
    assert_equal "stop", page.evaluate_script("fakeSkyblip.capture")
    page.execute_script("fakeSkyblip.press()")
    assert_selector "[role=status]", text: "Bravo closed the capture"
  end

  test "a phone that kept the smallest Bluetooth packet is told so on the settings page too" do
    visit "/configure"
    page.execute_script("fakeSkyblip.mtu = 23")
    click_button "Connect"
    assert_selector "[role=status]", text: "smallest packet size, 20 bytes. Disconnect and connect again"
  end

  private
    def connect_go
      click_button "Connect"
      assert_button "Disconnect"
    end
end
