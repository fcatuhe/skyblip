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
      assert_text "The USB step is done. Connect to Go 3F2A to verify the firmware."
      assert_text "Choose activate 3F2A skyBlip in the list."
      assert_selector ".setup-steps [aria-current=step]", text: "Connect"
      screen "13-go-installed-connect"

      click_button "Connect to verify"
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
      assert_no_select "Version"
      screen "20-bravo-choose-drive"

      click_button "Choose the drive"
      assert_text "Choose skyBlip Bravo 5C1D in the list."
      click_button "Connect to verify"
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

  test "a Bravo can retry the drive picker and must report the new build before setup finishes" do
    in_range
    plugged_in a_meshtastic_bravo(closePicker: true, bootedBuild: "0.1.0+40", cutOnClose: true)
    with_installers do
      visit "/briefing/setup"
      click_button "Install"
      click_button "Choose the drive"
      assert_text "No drive was selected."
      assert_text "Bravo: unplug it, hold its button"
      screen "24-bravo-drive-retry"

      click_button "Choose the drive again"
      assert_text "The USB step is done."
      screen "25-bravo-written"
      click_button "Connect to verify"
      assert_text "Briefing could not confirm the new firmware."
      assert_no_button "Done"
      assert_equal 1, page.evaluate_script("fakeUsb.restarts")
      assert_nil Event.find_by(name: "installer.verified")
      screen "26-bravo-not-verified"

      page.execute_script("#{unit("skyBlip Bravo 5C1D")}.running = fakeUsb.build")
      click_button "Connect to verify"
      assert_text "In the aircraft, switch the transponder to ALT"
      click_button "Done"
      assert_text "Bravo 5C1D is ready."
      assert_equal page.evaluate_script("fakeUsb.build"), journaled("installer.verified").payload["build"]
      assert_equal false, journaled("installer.finished").payload["saved"]
    end
  end

  test "a pilot who asked for development builds chooses one before the drive, the usual one already chosen" do
    in_range
    plugged_in a_meshtastic_bravo
    with_installers do
      visit "/briefing/preferences"
      check "Offer development builds"
      assert_link "See every build on GitHub."

      visit "/briefing/setup"
      click_button "Install"
      assert_select "Version", selected: "skyBlip dev 0.1.0+1860", options: [ "skyBlip dev 0.1.0+1860", "skyBlip dev 0.1.0+1858", "skyBlip dev 0.1.0+1854" ]
      screen "27-bravo-choose-build"

      click_button "Choose the drive"
      assert_text "Choose skyBlip Bravo 5C1D in the list."
      assert_equal "0.1.0+1860", journaled("installer.wrote").payload["version"]
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
      assert_selector ".device-figure--go svg[role=img]", text: /Pad\s+Button\s+RESET\s+LEDs\s+USB-C/
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
      assert_text "Go: press RESET twice, quickly. It is the upper button on its left side."
      assert_selector ".setup-by-hand svg[role=img][aria-label^='A skyBlip Go seen from the front, an arrow on its left side pointing at RESET']", text: "RESET, twice"
      assert_no_text "Bravo: unplug it"
      assert_no_selector ".setup-by-hand .device-figure--bravo"
      assert_no_selector ".unit-message"
      screen "50-not-answering"

      page.execute_script("fakeUsb.plug(#{a_meshtastic_go(mode: "bootloader").to_json})")
      click_button "Choose the drive"
      assert_text "Choose activate … skyBlip in the list."
      click_button "Connect to verify"
      assert_text "Name your aircraft."
      assert_equal [ [ { "namePrefix" => "activate" }, { "namePrefix" => "skyBlip Go" } ] ], page.evaluate_script("fakeFleet.asked")
    end
  end

  # An activated Go keeps its activation through an install over USB and advertises its address, so a chooser asking for activate only listed nothing.
  test "a Go activated before and installed again from its bootloader is listed at Connect, and goes on to its aircraft" do
    in_range
    plugged_in a_meshtastic_go(mode: "bootloader", unit: a_go)
    with_installers do
      visit "/briefing/setup"
      click_button "Install"
      click_button "Choose the drive"
      click_button "Connect to verify"
      assert_text "Name your aircraft."
      assert_equal [ [ { "namePrefix" => "activate" }, { "namePrefix" => "skyBlip Go" } ] ], page.evaluate_script("fakeFleet.asked")
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
      assert_no_text "Go: press RESET twice"
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
      assert_text "Go: press RESET twice, quickly. It is the upper button on its left side."
      screen "53-softrf-gesture"

      visit "/briefing/setup"
      page.execute_script("fakeUsb.plug(#{a_meshtastic_go(mode: "bootloader").to_json})")
      click_button "Install"
      assert_text "Go shows a USB drive named TECHOBOOT. Choose that drive in the next window, then let Briefing save to it.", wait: 1
      assert_no_text "press RESET"
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

  test "Chrome on Linux says what to set up once per computer, and where the drive is" do
    in_chrome_on "Linux"
    in_range
    plugged_in a_meshtastic_go
    with_installers do
      visit "/briefing/setup"
      within ".setup-linux" do
        assert_selector "h2", text: "On Linux, before you start"
        assert_button text: /sudo usermod -aG dialout \$USER\s+Copy/
        assert_text "On Arch Linux, the group is uucp."
        assert_button text: /sudo snap connect chromium:removable-media\s+Copy/
        assert_no_text "Do the Bluetooth steps on this page first."
      end
      screen "54-linux-start"

      click_button "Install"
      assert_text "Not in the window? Open the drive once in your file manager so Linux mounts it"
      click_button "Choose the drive"
      click_button "Connect to verify"
      assert_text "Name your aircraft."
    end
  end

  test "Chrome on Linux without Web Bluetooth is told to turn it on first, and where to connect after the restart" do
    in_chrome_on "Linux"
    in_range
    without_web_bluetooth
    plugged_in a_meshtastic_go
    with_installers do
      visit "/briefing/setup"
      within(".setup-linux") { assert_text "Turn on Web Bluetooth with the steps on this page first. Chrome restarts to apply it" }
      assert_selector ".bluetooth-check [data-check=flag]", text: "To do"
      screen "55-linux-start-without-web-bluetooth"

      click_button "Install"
      click_button "Choose the drive"
      assert_text "This computer cannot reach Go 3F2A over Bluetooth. Verify the firmware and finish setup on your phone."
      assert_text "Or turn on Web Bluetooth with the steps on this page. Once Chrome has restarted, open Your devices and connect it from there."
      assert_no_button "Connect to verify"
    end
  end

  test "Chrome on Linux without an adapter installs, then connects as soon as the adapter is back" do
    in_chrome_on "Linux"
    in_range adapter: false
    plugged_in a_meshtastic_go
    with_installers do
      visit "/briefing/setup"
      within(".setup-linux") { assert_no_text "Turn on Web Bluetooth" }
      click_button "Install"
      click_button "Choose the drive"
      assert_text "Or turn on Bluetooth with the steps on this page, and Connect to verify appears here."
      assert_no_button "Connect to verify"
      screen "56-linux-installed-without-adapter"

      page.execute_script("fakeFleet.plugAdapter(true)")
      assert_text "Choose activate 3F2A skyBlip in the list."
      click_button "Connect to verify"
      assert_text "Name your aircraft."
    end
  end

  test "a port Linux will not open names the group to join" do
    in_chrome_on "Linux"
    in_range
    plugged_in a_meshtastic_go(refusesOpen: true)
    with_installers do
      visit "/briefing/setup"
      click_button "Install"
      assert_selector ".unit-message", text: "or Linux does not let this browser open it. Close any program using it. If none is, run sudo usermod -aG dialout $USER in a terminal"
      assert_button "Install"
      screen "53-port-linux"
    end
  end

  test "a port Windows will not open says another program holds it" do
    in_chrome_on "Windows"
    in_range
    plugged_in a_meshtastic_go(refusesOpen: true)
    with_installers do
      visit "/briefing/setup"
      click_button "Install"
      assert_selector ".unit-message", text: "Another program is using the device. Close it, then click Install again."
      assert_no_text "dialout"
      assert_no_selector ".setup-linux"
    end
  end

  test "a computer without Bluetooth installs over USB, then hands the unit to a phone through a code, and keeps it there when the pilot comes back" do
    in_chrome_on "Windows"
    in_range adapter: false
    plugged_in a_meshtastic_go
    with_installers do
      visit "/briefing/setup"
      assert_text "Bluetooth is off or missing on this computer. Install from it anyway. The last step shows a code that carries the setup to your phone."
      screen "60-no-bluetooth-start"

      click_button "Install"
      click_button "Choose the drive"
      assert_text "This computer cannot reach Go 3F2A over Bluetooth. Verify the firmware and finish setup on your phone."
      assert_text "Briefing opens on this step: choose activate 3F2A skyBlip in the list."
      assert_selector ".setup-handoff img[src^='/briefing/setup/handoff?installed=go&tag=3f2a&build='][alt='Code that opens this setup on your phone']"
      assert_text "Or turn on Bluetooth with the steps on this page, and Connect to verify appears here."
      assert_no_button "Connect to verify"
      assert_equal({ "product" => "go", "cause" => "adapter" }, journaled("installer.handed_off").payload.slice("product", "cause"))
      screen "61-no-bluetooth-handoff"

      visit "/briefing/tools"
      visit "/briefing/setup"
      assert_selector ".setup-handoff img[src^='/briefing/setup/handoff?installed=go&tag=3f2a&build=']"
      assert_equal "tab", journaled("installer.resumed").payload["from"]
      click_button "Install another device"
      assert_text "Go 3F2A restarted as a USB drive named TECHOBOOT."
    end
  end

  test "a Go that can report its build but cannot send the registration offers a phone after a reconnect" do
    in_range a_new_go(mtu: 100)
    with_installers do
      visit "/briefing/setup?installed=go&tag=3f2a&build=0.1.0%2B1000"
      click_button "Connect to verify"
      assert_text "The Bluetooth link cannot carry the data needed to activate."
      assert_no_button "Continue on an Android phone"
      click_button "Try again"
      click_button "Continue on an Android phone"
      assert_selector "img[src='/briefing/setup/handoff?installed=go&tag=3f2a']"
      assert_equal false, page.evaluate_script("#{unit("3F2A skyBlip")}.connected")
      assert_equal "register", journaled("activation.noticed").payload["last_command"]
      screen "65-setup-activation-phone-handoff"
    end
  end

  test "a phone that opens the code connects the unit a computer installed, activates it with a press, and names the aircraft" do
    in_chrome_on "Android", mobile: true
    in_range a_new_go
    with_installers do
      visit "/briefing/setup?installed=go&tag=3f2a"
      assert_text "skyBlip is installed on Go 3F2A. Connect it from this phone to finish the setup."
      assert_text "Choose activate 3F2A skyBlip in the list."
      assert_selector ".setup-steps [aria-current=step]", text: "Connect"
      assert_no_button "Install another device"
      screen "62-phone-from-code"

      click_button "Connect"
      assert_text "Press the Go's button to activate it."
      page.execute_script("#{unit("3F2A skyBlip")}.press()")
      assert_text "Name your aircraft."
      assert_equal 3, Unit.find_by!(device_id: vectors["device_id"]).serial
      assert_equal [ [ { "name" => "activate 3F2A skyBlip" }, { "namePrefix" => "skyBlip Go" } ] ], page.evaluate_script("fakeFleet.asked")
      assert_equal({ "product" => "go", "from" => "code" }, journaled("installer.resumed").payload.slice("product", "from"))
    end
  end

  test "a Go whose link drops while its glass asks offers Try again, and activates on it" do
    in_chrome_on "Android", mobile: true
    in_range a_new_go
    with_installers do
      visit "/briefing/setup?installed=go&tag=3f2a"
      click_button "Connect"
      assert_text "Press the Go's button to activate it."
      page.execute_script("#{unit("3F2A skyBlip")}.drop()")
      assert_text "The connection dropped. Bring the device closer and reconnect. Activate again."
      screen "69-setup-link-lost"

      click_button "Try again"
      assert_text "Press the Go's button to activate it."
      page.execute_script("#{unit("3F2A skyBlip")}.press()")
      assert_text "Name your aircraft."
      assert_equal 3, Unit.find_by!(device_id: vectors["device_id"]).serial
    end
  end

  test "a Go the server refused asks the server again on Try again, and activates with no second press, since the first one trusted the browser" do
    in_chrome_on "Android", mobile: true
    in_range a_new_go
    Activation::Cap.current.update!(new_units_per_day: 1)
    units(:bound).update!(activated_at: Time.current)
    with_installers do
      visit "/briefing/setup?installed=go&tag=3f2a"
      click_button "Connect"
      assert_text "Press the Go's button to activate it."
      page.execute_script("#{unit("3F2A skyBlip")}.press()")
      assert_text "skyblip.eu reached its daily limit for new device activations. Try again tomorrow."
      screen "70-setup-server-refused"

      Activation::Cap.current.update!(new_units_per_day: 2)
      click_button "Try again"
      assert_text "Name your aircraft."
      assert_equal 3, Unit.find_by!(device_id: vectors["device_id"]).serial
    end
  end

  # Samsung Internet on an Android tablet offered Install, and its port chooser closed empty within 17 ms, six times over.
  test "a tablet whose browser has Web Serial connects a unit installed from a computer, and activates a new Go on the page" do
    in_chrome_on "Android"
    in_range a_new_go
    with_installers do
      visit "/briefing/setup"
      assert_text "Installed skyBlip from a computer? Switch the device on, then connect it from this phone to finish the setup."
      assert_selector "h2", text: "New device?"
      assert_link "Install it by hand instead"
      assert_no_button "Install"
      screen "63-phone"

      click_button "Connect it"
      assert_text "Press the Go's button to activate it."
      page.execute_script("#{unit("3F2A skyBlip")}.press()")
      assert_text "Name your aircraft."
      assert_current_path "/briefing/setup"
    end
  end

  test "an iPhone that opens the code is told where the setup goes on, with the link to copy" do
    page.driver.browser.page.command("Page.addScriptToEvaluateOnNewDocument", source: <<~JS)
      Object.defineProperty(navigator, 'userAgentData', { value: undefined });
      Object.defineProperty(navigator, 'userAgent', { value: 'Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Mobile/15E148 Safari/604.1' });
    JS
    without_web_bluetooth
    with_installers do
      visit "/briefing/setup?installed=go&tag=3f2a"
      assert_text "skyBlip is installed on Go 3F2A, but this browser cannot connect over Bluetooth. Open this page in Chrome on an Android phone, or on a computer with Bluetooth."
      assert_button "Copy the link"
      assert_no_link "Install it by hand instead"
      assert_no_selector ".setup-steps"
      screen "64-iphone-from-code"
    end
  end

  private
    def install_and_connect
      visit "/briefing/setup" unless page.current_path == "/briefing/setup"
      click_button "Install"
      click_button "Choose the drive"
      click_button "Connect to verify"
    end
end
