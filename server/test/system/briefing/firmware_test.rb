require "application_system_test_case"

class Briefing::FirmwareTest < ApplicationSystemTestCase
  test "a device behind the newest image of its track is offered it, and comes back on it by itself" do
    in_range a_go(mtu: 498, uploadDelayMs: 4, probationMs: 6000)
    on_the_shelf do
      connect_go
      within(".unit-banner") { click_link "Update" }
      assert_current_path "/briefing/devices/go-00012a/firmware"
      assert_button "Install 0.1.0+1005"
      screen "20-firmware-ready"

      click_button "Install 0.1.0+1005"
      assert_selector ".glass-title", text: "FIRMWARE"
      assert_selector ".glass-detail", text: "INSTALL 0.1.0+1005 FROM THE PHONE"
      screen "21-firmware-asking"

      page.execute_script("#{unit("00012A")}.press()")
      assert_selector ".firmware-status", text: "Sending 0.1.0+1005"
      assert_no_selector ".firmware-other"
      assert_no_selector ".unit-menu"
      assert_selector ".firmware-progress-text", text: /\d+ %, \d+ of 394 kB/
      screen "22-firmware-uploading"

      assert_selector ".firmware-status", text: "The device installs and restarts, about thirty seconds. It connects again by itself.", wait: 30
      screen "23-firmware-restarting"

      assert_selector ".firmware-status", text: "0.1.0+1005 is starting.", wait: 10
      screen "24-firmware-probation"

      assert_selector ".firmware-status.is-good", text: "Running 0.1.0+1005.", wait: 10
      assert_selector ".fleet-unit.is-linked", text: "Go 00012A"
      screen "25-firmware-done"

      click_link "Back to settings"
      assert_no_selector ".unit-banner"
      assert_selector ".unit-row", text: "0.1.0+1005, development · up to date"
    end
  end

  test "a device on the newest image is told so, and can still install a file of its own" do
    in_range a_go(running: "0.1.0+1005")
    on_the_shelf do
      connect_go
      assert_selector ".unit-row", text: "up to date"
      assert_no_selector ".unit-banner"
      click_link "Firmware"
      assert_selector ".firmware-status", text: "You run the newest image, 0.1.0+1005."
      find(".firmware-other summary").click
      assert_field "Or a file ending in .signed.bin"
      screen "26-firmware-current"
    end
  end

  test "an install refused on the glass says so, and asks nothing more until a reconnect" do
    in_range a_go
    on_the_shelf do
      connect_go
      click_link "Firmware"
      click_button "Install 0.1.0+1005"
      assert_selector ".glass-title", text: "FIRMWARE"
      page.execute_script("#{unit("00012A")}.refuse()")
      assert_selector ".unit-notes", text: "Refused on the device. Nothing changed, and it asks nothing more until you reconnect."
      assert_no_button "Install 0.1.0+1005"
      assert_button "Reconnect"
      screen "27-firmware-refused"
    end
  end

  test "the USB drive is one disclosure away, and asked on the glass" do
    in_range a_go
    on_the_shelf do
      connect_go
      click_link "Firmware"
      find(".firmware-recovery summary").click
      click_button "Restart into the USB drive"
      assert_selector ".glass-title", text: "RECOVERY"
      page.execute_script("#{unit("00012A")}.press()")
      assert_selector ".firmware-status", text: "The device is now a USB drive named TECHOBOOT"
      assert_no_button "Restart into the USB drive"
      screen "28-firmware-usb-drive"

      within(".unit") { click_button "Connect" }
      assert_selector ".unit-facts", text: "Connected"
      assert_equal 1, page.evaluate_script("fakeFleet.asked.length")
    end
  end

  test "the firmware page keeps the file picker when the published list is unavailable" do
    in_range a_go
    VCR.use_cassette("github/unavailable", allow_playback_repeats: true) do
      connect_go
      click_link "Firmware"
      find(".firmware-other summary").click
      assert_text "The published firmware list is unavailable. Choose a file instead."
      assert_field "Or a file ending in .signed.bin"
    end
  end

  private
    def connect_go
      visit "/briefing"
      click_button "Connect a device"
      assert_field "Callsign", with: "F-JXYZ"
    end
end
