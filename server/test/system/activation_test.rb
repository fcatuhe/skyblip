require "application_system_test_case"

class ActivationTest < ApplicationSystemTestCase
  setup do
    # INFO: fc 18oct26 on the page, not the browser, so the fake leaves at reset instead of following every later test
    script = Rails.root.join("test/system/fake_unit.js").read + "\nfakeUnit.vectors = #{vectors.to_json};"
    page.driver.browser.page.command("Page.addScriptToEvaluateOnNewDocument", source: script)
  end

  test "a unit gets its serial from skyblip.eu through the activation page" do
    visit "/activate"
    click_button "Connect"
    assert_selector "[data-activate-target=address]", text: "none yet"

    click_button "Activate"
    assert_selector "li[aria-current=step][data-step=confirm]"
    page.execute_script("fakeUnit.press()")

    assert_selector "[role=status]", text: "Activated: the unit holds its serial"
    assert_selector "[data-activate-target=address]", text: "000003"
    assert_equal 3, Unit.find_by!(device_id: vectors["device_id"]).serial
  end

  test "the aircraft type, the callsign and the own ICAO are saved in the same session, asked once on the glass" do
    activate
    assert_select "Aircraft type", selected: "Light aircraft"
    assert_field "Callsign", with: ""
    assert_field "Own ICAO address", with: ""

    select "Glider", from: "Aircraft type"
    fill_in "Callsign", with: "f-jxyz"
    fill_in "Own ICAO address", with: "3c6586"
    click_button "Save"
    assert_selector "li[aria-current=step][data-step=settings]"
    assert_equal({ "aircraft_type" => 4, "callsign" => "F-JXYZ", "own_icao" => 0x3C6586 }, page.evaluate_script("fakeUnit.staged"))

    page.execute_script("fakeUnit.press()")
    assert_selector "[role=status]", text: "Saved: the unit stored the settings you changed"
    assert_no_field "Callsign"
    assert_equal 1, page.evaluate_script("fakeUnit.commands").count("set")
  end

  test "skipping the settings keeps the ones the unit holds" do
    activate
    click_button "Skip"
    assert_selector "[role=status]", text: "Settings kept: the unit keeps its current aircraft type, callsign and own ICAO address"
    assert_no_field "Callsign"
    assert_not_includes page.evaluate_script("fakeUnit.commands"), "set"
  end

  test "a firmware that holds no own ICAO is offered none" do
    visit "/activate"
    page.execute_script("delete fakeUnit.stored.own_icao")
    activate
    assert_field "Callsign", with: ""
    assert_no_field "Own ICAO address"
    fill_in "Callsign", with: "F-JXYZ"
    click_button "Save"
    assert_equal({ "callsign" => "F-JXYZ" }, page.evaluate_script("fakeUnit.staged"))
  end

  test "an own ICAO that is not six hex digits is never sent" do
    activate
    fill_in "Own ICAO address", with: "3C65"
    click_button "Save"
    assert_nil page.evaluate_script("fakeUnit.staged")
    assert_not_includes page.evaluate_script("fakeUnit.commands"), "set"
    assert_selector "#activate-own-icao:invalid"
  end

  test "a unit with no fix since boot takes its settings indoors" do
    visit "/activate"
    page.execute_script("fakeUnit.flight = 'unknown'")
    activate
    fill_in "Callsign", with: "F-JXYZ"
    click_button "Save"
    assert_equal({ "callsign" => "F-JXYZ" }, page.evaluate_script("fakeUnit.staged"))
  end

  test "a unit in flight holds Save until it lands" do
    visit "/activate"
    page.execute_script("fakeUnit.flight = 'airborne'")
    activate
    fill_in "Callsign", with: "F-JXYZ"
    assert_selector "[data-activate-target=airborne]", text: "Save comes back once it has landed"
    assert_button "Save", disabled: true

    page.execute_script("fakeUnit.land()")
    assert_no_selector "[data-activate-target=airborne]"
    click_button "Save"
    assert_equal({ "callsign" => "F-JXYZ" }, page.evaluate_script("fakeUnit.staged"))
  end

  test "a browser that kept the smallest Bluetooth packet is told so at the first step" do
    visit "/activate"
    page.execute_script("fakeUnit.mtu = 23")
    click_button "Connect"
    click_button "Activate"
    assert_selector "[role=status]", text: "smallest packet size, or one too small for the answer"
    assert_equal %w[get challenge], page.evaluate_script("fakeUnit.commands")
    assert_nil Unit.find_by(device_id: vectors["device_id"])
  end

  private
    def activate
      visit "/activate" unless page.current_path == "/activate"
      click_button "Connect"
      click_button "Activate"
      assert_selector "li[aria-current=step][data-step=confirm]"
      page.execute_script("fakeUnit.press()")
      assert_selector "[role=status]", text: "Activated: the unit holds its serial"
    end
end
