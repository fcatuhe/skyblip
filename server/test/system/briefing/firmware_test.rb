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
      assert_glass "FIRMWARE"
      screen "21-firmware-asking"

      page.execute_script("#{unit("00012A")}.press()")
      assert_selector ".firmware-status", text: "Sending 0.1.0+1005"
      assert_no_selector ".firmware-other"
      assert_no_selector ".unit-menu"
      assert_selector ".firmware-progress-text", text: /\d+ %, \d+ of 394 kB/
      screen "22-firmware-uploading"

      assert_selector ".firmware-status", text: "The device is installing the firmware and restarting. This takes about thirty seconds. It connects again by itself.", wait: 30
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

  # The silence of #278: back on its old firmware, confirmed, and nothing on record.
  test "an install that comes back on the old firmware says so, and the report reaches skyBlip by itself" do
    in_range a_go(mtu: 498, uploadDelayMs: 4, failure: { image: "confirmed" })
    on_the_shelf do
      connect_go
      click_link "Firmware"
      click_button "Install 0.1.0+1005"
      page.execute_script("#{unit("00012A")}.press()")
      assert_selector ".firmware-status", text: "Sending 0.1.0+1005"
      assert_no_selector ".firmware-report"
      assert_selector ".firmware-status", text: "The device is installing the firmware and restarting", wait: 30
      assert_selector ".unit-message", text: "The device restarted on 0.1.0+1000 instead of 0.1.0+1005. It kept no record of the installation.", wait: 10
      assert_no_selector ".firmware-progress"
      assert_selector ".firmware-report", text: /skyBlip received the details of this failure\. .* give your support code [0-9A-Z]{4}-[0-9A-Z]{4}\./
      screen "28-firmware-no-record"

      reported = journaled("device.install_reported")
      assert_equal [ "no_record", "0.1.0+1000", "0.1.0+1005", false ], reported.payload.values_at("cause", "running", "tried", "report")
      assert_equal "go-00012a", reported.device
    end
  end

  test "a Bravo that reverted names how far the new firmware got, whenever the page connects" do
    report = { check: "matched", swap: "test", request_error: 0, boots: 2, probation_boots: 1, proof: "started", reset: "watchdog" }
    in_range a_bravo(image: "reverted", from: "0.1.0+40", to: "0.1.0+44", installReport: report)
    on_the_shelf do
      visit "/briefing"
      click_button "Connect a device"
      click_link "Firmware"
      assert_selector ".unit-message", text: "The new firmware started but stopped before completing its checks. The device went back to its previous firmware."
      assert_selector ".firmware-report", text: "skyBlip received the details of this failure."
      screen "29-firmware-reverted-report"

      reported = journaled("device.install_reported")
      assert_equal [ "probation_failed", "reverted", "0.1.0+40", "0.1.0+44", "matched", 1, "watchdog" ],
        reported.payload.values_at("cause", "image", "from", "to", "check", "probation_boots", "reset")
    end
  end

  test "a device on the newest image is told so, and can still install a file of its own" do
    in_range a_go(running: "0.1.0+1005")
    on_the_shelf do
      connect_go
      assert_selector ".unit-row", text: "up to date"
      assert_no_selector ".unit-banner"
      click_link "Firmware"
      assert_selector ".firmware-status", text: "Your device is running the latest image, 0.1.0+1005."
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
      assert_glass "FIRMWARE"
      page.execute_script("#{unit("00012A")}.refuse()")
      assert_selector ".unit-notes", text: "Refused on the device. Nothing changed. Reconnect before the device can ask again."
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
      assert_glass "RECOVERY"
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
      visit "/briefing"
      click_button "Connect a device"
      assert_field "Callsign", with: "F-JXYZ"
      click_link "Firmware"
      find(".firmware-other summary").click
      assert_text "The published firmware list is unavailable. Choose a file instead."
      assert_field "Or a file ending in .signed.bin"
    end
  end
end
