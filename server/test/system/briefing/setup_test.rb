require "application_system_test_case"

class Briefing::SetupTest < ApplicationSystemTestCase
  test "a Go running Meshtastic is installed over USB and activated with no press, because it trusts the browser that installed it" do
    in_range
    plugged_in a_meshtastic_go
    with_installers do
      visit "/briefing"
      assert_link "Set up a device"
      screen "10-go-home-first-visit"

      click_link "Set up a device"
      assert_current_path "/briefing/setup"
      assert_text "Plug your Go or Bravo into this computer with a USB cable."
      assert_selector ".setup-steps [aria-current=step]", text: "Install"
      screen "11-go-setup-start"

      click_button "Install"
      assert_text "Go 3F2A restarted as a USB drive named TECHOBOOT."
      assert_equal 1, page.evaluate_script("fakeUsb.restarts")
      screen "12-go-choose-drive"

      click_button "Choose the drive"
      assert_text "Installed. Last step: connect Go 3F2A over Bluetooth. It trusts this browser, so it asks for no button press."
      assert_text "Choose activate 3F2A skyBlip in the list."
      assert_selector ".setup-steps [aria-current=step]", text: "Connect"
      assert page.evaluate_script("fakeUsb.trusted !== null")
      screen "13-go-installed-connect"

      click_button "Connect"
      assert_current_path "/briefing/devices/go-000003?activated=1"
      assert_selector ".unit-notes .is-good", text: "Activated: skyBlip Go 000003."
      assert_no_selector ".glass-prompt:not([hidden])"
      assert_equal [ [ { "name" => "activate 3F2A skyBlip" } ] ], page.evaluate_script("fakeFleet.asked")
      assert_equal 3, Unit.find_by!(device_id: vectors["device_id"]).serial
      assert_selector ".tango", text: "Have a Bravo?"
      assert_link "Set up a Bravo", href: "/briefing/setup?for=go-000003"
      screen "14-go-activated"
    end
  end

  test "a Bravo running Meshtastic is installed over USB and opens on its transponder address, offering a Go" do
    in_range
    plugged_in a_meshtastic_bravo
    with_installers do
      visit "/briefing/setup"
      click_button "Install"
      assert_text "Bravo 5C1D restarted as a USB drive named X1."
      screen "20-bravo-choose-drive"

      click_button "Choose the drive"
      assert_text "Choose skyBlip Bravo 5C1D in the list."
      click_button "Connect"
      assert_current_path "/briefing/devices/bravo-5c1d?installed=1"
      assert_selector ".unit-notes .is-good", text: "skyBlip Bravo 5C1D runs skyBlip. If your aircraft has a transponder, add its address."
      assert_field "ADS-B address"
      assert_selector ".tango", text: "Have a Go?"
      assert_link "Set up a Go", href: "/briefing/setup?for=bravo-5c1d"
      screen "21-bravo-installed"
    end
  end

  test "a Go set up first, then its Bravo, pair by themselves with no press" do
    in_range
    plugged_in a_meshtastic_go
    with_installers do
      install_and_connect
      assert_current_path "/briefing/devices/go-000003?activated=1"
      click_link "Set up a Bravo"
      assert_current_path "/briefing/setup?for=go-000003"

      page.execute_script("fakeUsb.plug(#{a_meshtastic_bravo.to_json})")
      install_and_connect
      assert_current_path "/briefing/devices/bravo-5c1d?installed=1&pair=go-000003"
      assert_selector ".tango", text: "Pairing Go 000003 with Bravo 5C1D. Keep both close."
      screen "30-go-then-bravo-pairing"

      assert_selector ".tango", text: "Paired. Go 000003 shows the ADS-B traffic Bravo 5C1D hears."
      assert_equal 0x5c1d, page.evaluate_script("#{unit("Go 000003")}.pairedTag")
      assert_equal 0x3f2a, page.evaluate_script("#{unit("Bravo 5C1D")}.pairedTag")
      screen "31-go-then-bravo-paired"

      click_link "Go 000003"
      assert_selector ".tango", text: "Paired with Bravo 5C1D."
      screen "32-go-paired"
    end
  end

  test "a Bravo set up first, then its Go, pair by themselves once the Go is activated" do
    in_range
    plugged_in a_meshtastic_bravo
    with_installers do
      install_and_connect
      click_link "Set up a Go"
      assert_current_path "/briefing/setup?for=bravo-5c1d"

      page.execute_script("fakeUsb.plug(#{a_meshtastic_go.to_json})")
      install_and_connect
      assert_current_path "/briefing/devices/go-000003?activated=1&pair=bravo-5c1d"
      assert_selector ".tango", text: "Paired. Go 000003 shows the ADS-B traffic Bravo 5C1D hears."
      screen "40-bravo-then-go-paired"
    end
  end

  test "a unit that does not answer as Meshtastic is restarted by hand, with the gesture of each product" do
    in_range
    plugged_in a_meshtastic_go(mode: "silent")
    with_installers do
      visit "/briefing/setup"
      click_button "Install"
      assert_selector ".unit-message", text: "It did not answer as Meshtastic, so it did not restart by itself.", wait: 6
      assert_text "Go: press RST twice, quickly."
      assert_text "Bravo: unplug it, hold its button, plug it back in, and release once the light turns white."
      screen "50-not-meshtastic"

      page.execute_script("fakeUsb.plug(#{a_meshtastic_go(mode: "bootloader").to_json})")
      click_button "Choose the drive"
      assert_text "Choose activate … skyBlip in the list."
      click_button "Connect"
      assert_current_path "/briefing/devices/go-000003?activated=1"
      assert_equal [ [ { "namePrefix" => "activate" } ] ], page.evaluate_script("fakeFleet.asked")
    end
  end

  test "a closed port chooser says what to check, and a unit already in its bootloader skips the restart" do
    in_range
    plugged_in a_meshtastic_go(mode: "skyblip")
    with_installers do
      visit "/briefing/setup"
      click_button "Install"
      assert_selector ".unit-message", text: "Nothing listed? Switch the device on and try another cable: many USB-C cables only charge."
      screen "51-port-closed"

      page.execute_script("fakeUsb.plug(#{a_meshtastic_bravo(mode: "bootloader").to_json})")
      click_button "Use the drive"
      assert_text "Start its installer by hand. It shows up as a USB drive named TECHOBOOT or X1."
      click_button "Choose the drive"
      assert_text "Installed. Last step: connect Bravo over Bluetooth."
    end
  end

  test "a browser that cannot install over USB says where to install from" do
    in_range
    with_installers do
      page.driver.browser.page.command("Page.addScriptToEvaluateOnNewDocument", source: "delete Navigator.prototype.serial; Object.defineProperty(navigator, 'userAgentData', { value: { mobile: true, brands: [], platform: 'Android' } })")
      visit "/briefing/setup"
      assert_text "Install from a computer first, in Chrome or Edge. This phone then sets up and updates your devices over Bluetooth."
      assert_link "Install it by hand instead"
      assert_button "Connect it over Bluetooth"
      screen "52-phone"
    end
  end

  private
    def install_and_connect
      visit "/briefing/setup" unless page.current_path == "/briefing/setup"
      click_button "Install"
      click_button "Choose the drive"
      click_button "Connect"
    end
end
