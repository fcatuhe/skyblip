require "application_system_test_case"

class Briefing::SetupTest < ApplicationSystemTestCase
  test "a Go running Meshtastic is installed, activated with no press and named, because it trusts the browser that installed it" do
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
      screen "13-go-installed-connect"

      click_button "Connect"
      assert_text "Name your aircraft. Other pilots see its callsign and type."
      assert_selector ".setup-steps [aria-current=step]", text: "Your aircraft"
      assert_no_selector ".glass-prompt:not([hidden])"
      assert_equal 3, Unit.find_by!(device_id: vectors["device_id"]).serial
      assert_button "Done"
      screen "14-go-aircraft"

      select "Glider", from: "Aircraft type"
      fill_in "Callsign", with: "F-CABC"
      click_button "Save"
      assert_text "Go 000003 is ready. Its settings stay on its page."
      assert_link "Set up a Bravo", href: "/briefing/setup?for=go-000003"
      assert_link "New to the Go? Read its first steps.", href: "/go-first-steps"
      assert_no_link "New to Bravo? Read its first steps."
      assert_equal({ "aircraft_type" => 4, "callsign" => "F-CABC" }, page.evaluate_script("#{unit("Go 000003")}.stored").slice("aircraft_type", "callsign"))
      screen "15-go-ready"

      click_link "Open Go 000003"
      assert_current_path "/briefing/devices/go-000003"
      assert_field "Callsign", with: "F-CABC"
      assert_selector ".tango", text: "Have a Bravo?"
    end
  end

  test "a Bravo running Meshtastic is installed, and its transponder address waits for the aircraft" do
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
      assert_text "In the aircraft, switch the transponder to ALT and open Briefing: it offers the address Bravo hears"
      assert_no_field "Callsign"
      screen "21-bravo-aircraft"

      click_button "Done"
      assert_text "Bravo 5C1D is ready."
      assert_link "Set up a Go", href: "/briefing/setup?for=bravo-5c1d"
      assert_link "New to Bravo? Read its first steps.", href: "/bravo-first-steps"
      assert_no_link "New to the Go? Read its first steps."
      screen "22-bravo-ready"

      click_link "New to Bravo? Read its first steps."
      assert_current_path "/bravo-first-steps"
      assert_selector ".device-figure--bravo svg[role=img]", text: /Button\s+LED\s+USB-C/
      assert_selector "table .lamp--white"
      screen "23-bravo-first-steps"
    end
  end

  test "a Go set up first, then its Bravo, pair with no press, and the Go names the aircraft from its transponder later" do
    in_range
    plugged_in a_meshtastic_go
    with_installers do
      install_and_connect
      click_button "Done"
      click_link "Set up a Bravo"
      assert_current_path "/briefing/setup?for=go-000003"

      page.execute_script("fakeUsb.plug(#{a_meshtastic_bravo.to_json})")
      install_and_connect
      assert_text "Pairing Go 000003 with Bravo 5C1D. Keep both close."
      screen "30-go-then-bravo-pairing"

      assert_text "Nothing to type. In the aircraft, switch the transponder to ALT: the Go offers its address, callsign and type from what Bravo hears"
      assert_equal 0x5c1d, page.evaluate_script("#{unit("Go 000003")}.pairedTag")
      assert_equal 0x3f2a, page.evaluate_script("#{unit("Bravo 5C1D")}.pairedTag")
      screen "31-tango-aircraft"

      click_button "Done"
      assert_text "Your Tango is ready: Go 000003 shows the ADS-B traffic Bravo 5C1D hears."
      assert_no_link "Set up a Go"
      assert_link "New to the Go? Read its first steps."
      assert_link "New to Bravo? Read its first steps."
      screen "32-tango-ready"

      click_link "Open Go 000003"
      assert_selector ".tango", text: "Paired with Bravo 5C1D."

      visit "/go-first-steps"
      assert_selector ".device-figure--go svg[role=img]", text: /Pad\s+Button\s+RST\s+LEDs\s+USB-C/
      screen "33-go-first-steps"
    end
  end

  test "a Bravo set up first, then its Go, pair once the Go is activated, and the aircraft can still be named at once" do
    in_range
    plugged_in a_meshtastic_bravo
    with_installers do
      install_and_connect
      click_button "Done"
      click_link "Set up a Go"
      assert_current_path "/briefing/setup?for=bravo-5c1d"

      page.execute_script("fakeUsb.plug(#{a_meshtastic_go.to_json})")
      install_and_connect
      assert_text "Nothing to type."
      click_button "Set them now"
      fill_in "Callsign", with: "F-CABC"
      fill_in "ADS-B address", with: "3c6586"
      screen "40-bravo-then-go-set-now"

      click_button "Save"
      assert_text "Your Tango is ready: Go 000003 shows the ADS-B traffic Bravo 5C1D hears."
      assert_equal 0x3c6586, page.evaluate_script("#{unit("Go 000003")}.stored.aircraft_address")
    end
  end

  test "a unit that does not answer over USB gets the drive by hand, with its product's gesture" do
    in_range
    plugged_in a_meshtastic_go(mode: "silent")
    with_installers do
      visit "/briefing/setup"
      click_button "Install"
      assert_text "Start the installer of Go by hand, then choose the USB drive named TECHOBOOT in the next window:", wait: 6
      assert_text "Go: press RST twice, quickly. It is the upper button on its left side."
      assert_selector ".setup-by-hand svg[role=img][aria-label^='A skyBlip Go seen from the front, an arrow on its left side pointing at RST']", text: "RST, twice"
      assert_no_text "Bravo: unplug it"
      assert_no_selector ".setup-by-hand .device-figure--bravo"
      assert_no_selector ".unit-message"
      screen "50-not-answering"

      page.execute_script("fakeUsb.plug(#{a_meshtastic_go(mode: "bootloader").to_json})")
      click_button "Choose the drive"
      assert_text "Choose activate … skyBlip in the list."
      click_button "Connect"
      assert_text "Name your aircraft."
      assert_equal [ [ { "namePrefix" => "activate" } ] ], page.evaluate_script("fakeFleet.asked")
    end
  end

  test "a Bravo that does not answer over USB gets its button pictured, held while the cable goes in until the LED above it turns white" do
    in_range
    plugged_in a_meshtastic_bravo(mode: "silent")
    with_installers do
      visit "/briefing/setup"
      click_button "Install"
      assert_text "Bravo: unplug it, hold its button, plug it back in, and release once the LED above the button turns white.", wait: 6
      within ".setup-by-hand .device-figure--bravo" do
        assert_selector "svg[role=img][aria-label='A skyBlip Bravo, its round button marked hold, the LED just above it marked, and its USB-C port marked plug in.']", text: /Hold\s+LED\s+Plug in/
        assert_selector ".lamp.lamp--white.lamp--steady"
        assert_text "Release once the LED above the button turns white."
      end
      assert_no_text "Go: press RST twice"
      screen "55-bravo-gesture"
    end
  end

  test "a T-Echo on a firmware that cannot restart into its drive gets the gesture at once, and one already in its bootloader goes straight to the drive" do
    in_range
    plugged_in a_meshtastic_go(mode: "softrf")
    with_installers do
      visit "/briefing/setup"
      click_button "Install"
      assert_text "Start the installer of Go by hand", wait: 1
      assert_text "Go: press RST twice, quickly. It is the upper button on its left side."
      screen "53-softrf-gesture"

      visit "/briefing/setup"
      page.execute_script("fakeUsb.plug(#{a_meshtastic_go(mode: "bootloader").to_json})")
      click_button "Install"
      assert_text "Go shows a USB drive named TECHOBOOT. Choose that drive in the next window, then let Briefing save to it.", wait: 1
      assert_no_text "press RST"
      screen "54-already-bootloader"
      assert_equal 0, page.evaluate_script("fakeUsb.restarts")
    end
  end

  test "a closed port chooser says what to check, and offers Bluetooth for a unit already running skyBlip" do
    in_range a_go
    plugged_in a_meshtastic_go(mode: "skyblip")
    with_installers do
      visit "/briefing/setup"
      click_button "Install"
      assert_selector ".unit-message", text: "Nothing listed? Switch the device on and try another cable: many USB-C cables only charge. A unit already running skyBlip lists no USB port: connect it over Bluetooth."
      screen "51-port-closed"

      click_button "Connect it over Bluetooth"
      assert_current_path "/briefing/devices/go-00012a"
      assert_selector ".fleet-unit", text: /is out|up to date|needs its firmware/, wait: 15
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
