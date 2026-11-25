require "application_system_test_case"

class Briefing::BluetoothCheckTest < ApplicationSystemTestCase
  test "Chrome on Linux lists the flag to turn on, offers no connection until it is on, and still sets a device up over USB" do
    in_chrome_on "Linux"
    visit "/briefing"
    within ".bluetooth-check" do
      assert_selector "h2", text: "Switch on Bluetooth in this browser"
      assert_selector "[data-check=flag]", text: "Turn on Web Bluetooth To do"
      assert_button text: %r{\Achrome://flags/#enable-web-bluetooth\s+Copy\z}
      assert_selector "[data-check=adapter]", text: "Turn on Bluetooth Checked once Web Bluetooth is on"
      assert_text "sudo systemctl enable --now bluetooth"
      assert_selector "[data-check=remember]", text: "Reconnect your devices by themselves Checked once Web Bluetooth is on"
    end
    assert_no_selector ".fleet-unsupported"
    assert_no_button "Connect a device"
    assert_link "Set up a device"
    screen "20-bluetooth-flag"
  end

  test "an adapter that comes back clears the list, and the device connects and installs" do
    in_chrome_on "Linux"
    in_range a_go, adapter: false
    on_the_shelf do
      visit "/briefing"
      within ".bluetooth-check" do
        assert_selector "[data-check=flag]", text: "Done"
        assert_selector "[data-check=adapter]", text: "Turn on Bluetooth To do"
        assert_selector "[data-check=remember]", text: "Optional"
      end
      assert_no_button "Connect a device"
      screen "21-bluetooth-adapter"

      page.execute_script("fakeFleet.plugAdapter(true)")
      assert_no_selector ".bluetooth-check"
      click_button "Connect a device"
      assert_current_path "/briefing/devices/go-00012a"
      assert_selector ".unit-banner", text: "0.1.0+1765 is out."
    end
  end

  test "Chrome on macOS shows no list while its adapter is present" do
    in_chrome_on "macOS"
    in_range a_go
    visit "/briefing"
    assert_button "Connect a device"
    assert_no_selector ".bluetooth-check"
  end

  test "a device page holds its connect button while Windows has no adapter" do
    in_chrome_on "Windows"
    in_range a_go, adapter: false
    on_the_shelf do
      visit "/briefing/devices/go-00012a"
      within(".bluetooth-check") { assert_selector "[data-check=adapter]", text: "To do" }
      assert_no_selector "[data-check=flag]", visible: true
      assert_no_text "sudo systemctl"
      within(".unit") { assert_no_button "Connect" }
      screen "22-bluetooth-device-page"

      page.execute_script("fakeFleet.plugAdapter(true)")
      assert_no_selector ".bluetooth-check"
      within(".unit") { click_button "Connect" }
      assert_field "Callsign", with: "F-JXYZ"
    end
  end
end
