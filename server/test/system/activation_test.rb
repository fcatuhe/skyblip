require "application_system_test_case"

class ActivationTest < ApplicationSystemTestCase
  setup do
    page.driver.browser.evaluate_on_new_document(Rails.root.join("test/system/fake_unit.js").read + "\nfakeUnit.vectors = #{vectors.to_json};")
  end

  test "a unit gets its serial from skyblip.eu through the activation page" do
    visit "/activate"
    click_button "Connect"
    assert_selector "[data-activate-target=address]", text: "none yet"

    click_button "Activate"
    assert_selector "li[aria-current=step][data-step=confirm]"
    page.execute_script("fakeUnit.press()")

    assert_selector "[role=status]", text: "Activated: the unit holds its serial"
    assert_selector "[data-activate-target=address]", text: "000002"
    assert_equal 2, Unit.find_by!(device_id: vectors["device_id"]).serial
  end
end
