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

  test "a browser that kept the smallest Bluetooth packet is told so at the first step" do
    visit "/activate"
    page.execute_script("fakeUnit.mtu = 23")
    click_button "Connect"
    click_button "Activate"
    assert_selector "[role=status]", text: "smallest packet size, or one too small for the answer"
    assert_equal %w[get challenge], page.evaluate_script("fakeUnit.commands")
    assert_nil Unit.find_by(device_id: vectors["device_id"])
  end
end
