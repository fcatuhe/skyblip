require "application_system_test_case"

class Briefing::DevicesTest < ApplicationSystemTestCase
  GO_PROTOCOLS = {
    adsl_version: "0.1.0", adsl_specification: "ADS-L 4 SRD-860 Issue 2",
    alptas_version: "0.1.0", alptas_specification: "ALP-TAS 2024 FLARM-generation frame"
  }.freeze

  test "a first visit offers one thing, and a Go connected there opens on its settings" do
    in_range a_go
    on_the_shelf do
      visit "/briefing"
      assert_selector "h1", text: "Your skyBlip"
      assert_no_selector ".fleet-unit"
      screen "01-first-visit"

      click_button "Connect a device"
      assert_current_path "/briefing/devices/go-00012a"
      assert_selector ".unit-facts", text: "Connected · 82 % · On the ground · 0.1.0+1000, development"
      assert_field "Callsign", with: "F-JXYZ"
      assert_select "Alarm volume", selected: "Soft"
      assert_selector ".fleet-unit.is-linked", text: "Go 00012A"
      assert_selector ".unit-banner", text: "0.1.0+1005 is out. Your device is running 0.1.0+1000."
      screen "02-device-connected"
    end
  end

  test "a device this browser met opens by itself after a reload, with what it last read, and connects from there" do
    in_range a_go
    on_the_shelf do
      visit "/briefing"
      click_button "Connect a device"
      assert_selector ".unit-banner", text: "0.1.0+1005 is out."

      visit "/briefing"
      assert_current_path "/briefing/devices/go-00012a"
      assert_selector ".unit-last", text: /Callsign\s*F-JXYZ/
      assert_selector ".unit-banner", text: "0.1.0+1005 is out. Your device is running 0.1.0+1000."
      assert_no_link "Update"
      screen "04-device-not-connected"

      find(".app-bar-brand").click
      assert_selector "h1", text: "Your devices"
      assert_selector ".fleet-unit", text: /Go 00012A\s*F-JXYZ · Not connected · seen [^\n]*\n0.1.0\+1005 is out/
      assert_no_selector ".fleet-activate"
      screen "03-home-remembered"

      click_link "Go 00012A"
      within(".unit") { click_button "Connect" }
      assert_field "Callsign", with: "F-JXYZ"
      assert_equal [ [ { "name" => "skyBlip Go 00012A" }, { "name" => "skyBlip Go+ 00012A" } ] ], page.evaluate_script("fakeFleet.asked")
    end
  end

  test "a Go and a Bravo stay connected side by side, each on its own screen" do
    in_range a_go, a_bravo
    on_the_shelf do
      visit "/briefing"
      click_button "Connect a device"
      assert_field "Callsign", with: "F-JXYZ"

      page.execute_script("fakeFleet.pick = 'skyBlip Bravo 3F2A'")
      within(".fleet--rail") { click_button "Connect a device" }
      assert_current_path "/briefing/devices/bravo-3f2a"
      assert_selector ".fleet-unit.is-linked", count: 2
      assert_no_field "Callsign"
      assert_select "Alarm volume", selected: "Soft"
      screen "05-go-and-bravo"

      click_link "Go 00012A"
      assert_field "Callsign", with: "F-JXYZ"
      assert_selector ".fleet-unit.is-linked", count: 2
    end
  end

  test "a change is saved after two presses on the glass, and the page shows what the device stored" do
    in_range a_go
    on_the_shelf do
      connect_go
      fill_in "Callsign", with: "f-abcd"
      select "Loud", from: "Alarm volume"
      assert_button "Save 2 changes"
      screen "06-settings-edited"

      click_button "Save 2 changes"
      assert_selector ".glass-prompt .glass-title", text: "SETTINGS"
      assert_selector ".glass-press", text: "Press the Go's button twice."
      assert_selector ".glass-left", text: /REFUSED IN (30|29) S/
      assert_no_field "Callsign"
      screen "07-glass-asking"

      page.execute_script("#{unit("00012A")}.press()")
      assert_selector ".unit-message", text: "Saved on the device."
      assert_field "Callsign", with: "F-ABCD"
      assert_select "Alarm volume", selected: "Loud"
      assert_equal({ "callsign" => "F-ABCD", "alarm_volume" => "loud" }, page.evaluate_script("#{unit("00012A")}.staged"))
      screen "08-settings-saved"
    end
  end

  test "back to defaults fills the form with the firmware's values, and Save sends them" do
    in_range a_go
    on_the_shelf do
      connect_go
      click_button "Back to defaults (3)"
      assert_select "Aircraft type", selected: "Light aircraft"
      assert_field "Callsign", with: ""
      assert_select "Alarm volume", selected: "Loud"
      click_button "Save 3 changes"
      page.execute_script("#{unit("00012A")}.press()")
      assert_selector ".unit-message", text: "Saved on the device."
      assert_equal({ "aircraft_type" => 1, "callsign" => "", "alarm_volume" => "loud" }, page.evaluate_script("#{unit("00012A")}.staged"))
      assert_no_selector "[data-unit-target=reset]"
    end
  end

  test "a press that refuses holds every question until a reconnect, which needs no chooser" do
    in_range a_go
    on_the_shelf do
      connect_go
      select "Loud", from: "Alarm volume"
      click_button "Save 1 change"
      assert_selector ".glass-prompt"
      page.execute_script("#{unit("00012A")}.refuse()")
      assert_selector ".unit-notes", text: "Refused on the device. Nothing changed. Reconnect before the device can ask again."
      assert_no_selector ".unit-message"
      assert_no_button "Save 1 change"
      assert_no_selector ".unit-banner"
      screen "09-refused-on-device"

      within(".unit-settings") { click_button "Reconnect" }
      assert_no_selector ".unit-notes", text: "It asks nothing more"
      assert_button "Save", disabled: true
      assert_equal 1, page.evaluate_script("fakeFleet.asked.length")
    end
  end

  test "a save two presses allowed trusts this browser, which then saves with no prompt and lists the trusted browsers" do
    in_range a_go(browsers: [ { id: "0102030405060708", label: "Edge on Windows" } ])
    on_the_shelf do
      connect_go
      assert_no_selector "[data-unit-target=browsers]"
      select "Loud", from: "Alarm volume"
      click_button "Save 1 change"
      assert_selector ".glass-remembers", text: "Allowing it also adds this browser to the trusted list"
      screen "15-glass-trusts"
      page.execute_script("#{unit("00012A")}.press()")
      assert_selector ".unit-message", text: "Saved on the device."

      within("[data-unit-target=browsers]") do
        assert_selector "li", count: 2
        assert_selector "li", text: "Edge on Windows"
        assert_selector "li", text: "· this browser"
      end
      screen "16-trusted-browsers"

      select "Soft", from: "Alarm volume"
      click_button "Save 1 change"
      assert_selector ".unit-message", text: "Saved on the device."
      assert_no_selector ".glass-prompt"

      click_button "Forget the other browsers"
      within("[data-unit-target=browsers]") { assert_selector "li", count: 1, text: "this browser" }
      assert_no_button "Forget the other browsers"
      assert_equal 1, page.evaluate_script("#{unit("00012A")}.browsers.length")
    end
  end

  test "a trusted browser changes settings in flight, and the notes say updates wait for the landing" do
    in_range a_go(browsers: [])
    on_the_shelf do
      connect_go
      select "Loud", from: "Alarm volume"
      click_button "Save 1 change"
      page.execute_script("#{unit("00012A")}.press()")
      assert_selector "[data-unit-target=browsers] li", text: "this browser"

      page.execute_script("#{unit("00012A")}.takeOff()")
      assert_selector ".unit-notes", text: "This browser can still change the device's settings. Other actions are available again after landing."
      screen "17-trusted-airborne"
      select "Soft", from: "Alarm volume"
      click_button "Save 1 change"
      assert_selector ".unit-message", text: "Saved on the device."
      assert_equal "soft", page.evaluate_script("#{unit("00012A")}.stored.alarm_volume")
    end
  end

  test "a device in flight holds every change until it lands" do
    in_range a_go
    on_the_shelf do
      connect_go
      page.execute_script("#{unit("00012A")}.takeOff()")
      assert_selector ".unit-facts", text: "Airborne"
      assert_selector ".unit-notes", text: "Changes are available again after landing."
      assert_selector "fieldset[disabled]"
      assert_button "Locked in flight", disabled: true
      screen "10-airborne"

      page.execute_script("#{unit("00012A")}.land()")
      assert_no_selector "fieldset[disabled]"
    end
  end

  test "a Bravo in flight locks its capture too, and offers no stop" do
    in_range a_bravo
    on_the_shelf do
      visit "/briefing"
      click_button "Connect a device"
      assert_select "Profile"
      page.execute_script("#{unit("3F2A")}.takeOff()")
      assert_selector ".unit-facts", text: "Airborne"
      within("[data-unit-target=capture]") do
        assert_button "Locked in flight", disabled: true
        assert_no_button "Stop the capture"
      end

      page.execute_script("#{unit("3F2A")}.land()")
      within("[data-unit-target=capture]") { assert_button "Arm", disabled: false }
    end
  end

  test "a Bravo asks with its lamp, since it has no glass, and is never said to need activation" do
    in_range a_bravo
    on_the_shelf do
      visit "/briefing"
      click_button "Connect a device"
      assert_current_path "/briefing/devices/bravo-3f2a"
      select "Loud", from: "Alarm volume"
      click_button "Save 1 change"
      assert_selector ".glass-lamp", text: "The LED blinks green fast. One press refuses."
      assert_selector ".glass-press", text: "Press the Bravo's button twice."
      assert_no_selector ".glass"
      assert_no_text "Not activated"
      screen "11-bravo-lamp"
    end
  end

  test "a Bravo arms and stops a capture with two presses on its lamp, and a Go offers none" do
    in_range a_go, a_bravo
    on_the_shelf do
      connect_go
      assert_no_select "Profile"

      page.execute_script("fakeFleet.pick = 'skyBlip Bravo 3F2A'")
      within(".fleet--rail") { click_button "Connect a device" }
      assert_current_path "/briefing/devices/bravo-3f2a"
      select "Power run", from: "Profile"
      click_button "Arm"
      assert_selector ".glass-lamp", text: "The LED blinks green fast."
      assert_selector ".glass-press", text: "Press the Bravo's button twice."
      assert_no_select "Profile"
      assert_equal "power_run", page.evaluate_script("#{unit("3F2A")}.capture")

      page.execute_script("#{unit("3F2A")}.press()")
      assert_selector ".unit-message.is-good", text: "Armed. The LED blinks twice every 3 s"
      click_button "Arm"
      assert_selector ".unit-message", text: "A capture is already running. Stop it first."

      click_button "Stop the capture"
      page.execute_script("#{unit("3F2A")}.press()")
      assert_selector ".unit-message.is-good", text: "Stopped. The LED is back to its single blink."
      assert_equal false, page.evaluate_script("#{unit("3F2A")}.capturing")
    end
  end

  test "a capture refused on Bravo's button arms nothing, and asks nothing more until a reconnect" do
    in_range a_bravo
    on_the_shelf do
      visit "/briefing"
      click_button "Connect a device"
      click_button "Arm"
      assert_selector ".glass-lamp"
      page.execute_script("#{unit("3F2A")}.refuse()")
      assert_selector ".unit-notes", text: "Refused on the device. Nothing changed. Reconnect before the device can ask again."
      assert_no_button "Arm"
      assert_equal false, page.evaluate_script("Boolean(#{unit("3F2A")}.capturing)")
    end
  end

  test "the own ICAO is typed in hex, and half an address is never sent" do
    in_range a_go
    on_the_shelf do
      connect_go
      fill_in "Own ICAO address", with: "3C65"
      click_button "Save 1 change"
      assert_selector "#unit-own-icao:invalid"
      assert_nil page.evaluate_script("#{unit("00012A")}.staged ?? null")

      fill_in "Own ICAO address", with: "3c6586"
      click_button "Save 1 change"
      page.execute_script("#{unit("00012A")}.press()")
      assert_field "Own ICAO address", with: "3C6586"
      assert_equal({ "own_icao" => 0x3C6586 }, page.evaluate_script("#{unit("00012A")}.staged"))
    end
  end

  test "a Go that holds no address yet is sent to its activation from its own page" do
    in_range a_go(address: nil)
    on_the_shelf do
      visit "/briefing/devices/go-00012a"
      within(".unit") { click_button "Connect" }
      within(".unit-notes") { click_link "Activate" }
      assert_current_path "/briefing/activate"
      assert_button "Activate"
    end
  end

  test "a phone that kept the smallest Bluetooth packet is told what to do" do
    in_range a_go(mtu: 23)
    on_the_shelf do
      visit "/briefing"
      click_button "Connect a device"
      assert_selector ".unit-notes", text: "is using the smallest Bluetooth packet"
    end
  end

  test "a browser without Web Bluetooth says where to open the app, and offers nothing it cannot do" do
    visit "/briefing"
    assert_selector ".fleet-unsupported h1", text: "Open this on Android or a computer"
    assert_no_button "Connect a device"
    screen "12-no-bluetooth"
  end

  test "forgetting a device asks first, then drops it from this browser" do
    in_range a_go
    on_the_shelf do
      connect_go
      find(".unit-menu summary").click
      click_button "Forget this device"
      within("dialog[open]") do
        assert_text "Forget skyBlip Go 00012A?"
        screen "14-forget"
        click_button "Keep"
      end
      assert_no_selector "dialog[open]"
      assert_field "Callsign", with: "F-JXYZ"

      find(".unit-menu summary").click
      click_button "Forget this device"
      within("dialog[open]") { click_button "Forget this device" }
      assert_current_path "/briefing"
      assert_selector "h1", text: "Your skyBlip"
      assert_no_selector ".fleet-unit"
    end
  end

  test "a connected device names the radio protocol versions it carries" do
    in_range a_go(protocols: GO_PROTOCOLS)
    on_the_shelf do
      visit "/briefing"
      click_button "Connect a device"
      assert_selector ".unit-protocols", text: "Radio protocols: ADS-L 0.1.0 (ADS-L 4 SRD-860 Issue 2), ALP-TAS 0.1.0 (ALP-TAS 2024 FLARM-generation frame)"

      find(".unit-menu summary").click
      click_button "Disconnect"
      assert_selector ".unit-facts", text: "Not connected"
      assert_no_selector ".unit-protocols"
    end
  end

  test "firmware from before the protocols frame names none, and the page says nothing about it" do
    in_range a_go
    on_the_shelf do
      visit "/briefing"
      click_button "Connect a device"
      assert_field "Callsign", with: "F-JXYZ"
      assert_no_selector ".unit-protocols"
      assert_no_text "too old"
    end
  end

  test "the app speaks French once the pilot chooses it in the settings" do
    in_range a_go(protocols: GO_PROTOCOLS)
    on_the_shelf do
      visit "/briefing"
      find("summary", text: "Menu").click
      click_link "Settings"
      screen "15-settings"
      choose "Français"
      assert_selector "h1", text: "Paramètres"

      click_link "Retour à vos appareils"
      assert_selector "h1", text: "Votre skyBlip"
      click_button "Connecter un appareil"
      assert_current_path "/briefing/devices/go-00012a"
      assert_selector ".unit-facts", text: "Connecté · 82 %"
      assert_selector ".unit-protocols", text: "Protocoles radio : ADS-L 0.1.0 (ADS-L 4 SRD-860 Issue 2)"
      assert_selector ".unit-banner", text: "0.1.0+1005 est sortie. Vous avez 0.1.0+1000."
      assert_field "Indicatif", with: "F-JXYZ"
      screen "13-french"
    end
  end
end
