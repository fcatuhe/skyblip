require "application_system_test_case"

class Briefing::TangoTest < ApplicationSystemTestCase
  test "a Go and a Bravo connected side by side are offered the pairing, and units that do not trust this browser ask for their presses" do
    in_range a_go(bluetoothTag: 0x1b2c), a_bravo(name: "skyBlip Bravo 5C1D", bluetoothTag: 0x5c1d)
    on_the_shelf do
      connect_both
      assert_selector ".tango", text: "Bravo 5C1D is connected. Pair it with this Go, and the Go shows the ADS-B traffic it hears."
      screen "60-pair-offer"

      click_button "Pair with Bravo 5C1D"
      assert_selector ".tango", text: "Press the button on Go 00012A to confirm."
      assert_no_selector ".tango .device-figure"
      assert_glass "PAIR BRAVO"
      screen "61-pair-confirm-on-go"

      page.execute_script("#{unit("Go 00012A")}.press()")
      assert_selector ".tango", text: "Press the button on Bravo 5C1D when its light blinks blue."
      within ".tango .device-figure--bravo" do
        assert_selector "svg[role=img][aria-label='A skyBlip Bravo, its round button marked press.']", text: "Press"
        assert_selector ".lamp.lamp--blue"
        assert_text "The LED blinks blue fast."
      end
      screen "62-pair-press-bravo"

      page.execute_script("#{unit("Bravo 5C1D")}.pressPairing()")
      assert_selector ".tango", text: "Paired. Go 00012A shows the ADS-B traffic Bravo 5C1D hears."
      assert_equal 0x5c1d, page.evaluate_script("#{unit("Go 00012A")}.pairedTag")
      assert_no_selector ".tango .device-figure"
      screen "63-paired-after-presses"
    end
  end

  test "a Bravo this browser met but not connected is offered to connect, then to pair" do
    in_range a_go(bluetoothTag: 0x1b2c), a_bravo(name: "skyBlip Bravo 5C1D", bluetoothTag: 0x5c1d)
    on_the_shelf do
      connect_both
      page.execute_script("#{unit("Bravo 5C1D")}.drop()")
      assert_selector ".tango", text: "Bravo 5C1D can pair with this Go. Switch it on, then connect it."
      screen "64-partner-away"

      click_button "Connect Bravo 5C1D"
      assert_button "Pair with Bravo 5C1D"
    end
  end

  test "a Go paired with another Bravo offers to pair with the one connected instead" do
    in_range a_go(bluetoothTag: 0x1b2c, pairedTag: 0x0a0b), a_bravo(name: "skyBlip Bravo 5C1D", bluetoothTag: 0x5c1d)
    on_the_shelf do
      connect_both
      assert_selector ".tango", text: "Paired with Bravo 0A0B. Pair with Bravo 5C1D instead?"
      assert_button "Pair with Bravo 5C1D instead"
      screen "65-pair-instead"
    end
  end

  test "a Go that does not find its Bravo says so, and offers to try again" do
    in_range a_go(bluetoothTag: 0x1b2c, pressAfterMs: 50), a_bravo(name: "skyBlip Bravo 5C1D", bluetoothTag: 0x5c1d)
    on_the_shelf do
      connect_both
      page.execute_script("#{unit("Bravo 5C1D")}.rebooting = true")
      click_button "Pair with Bravo 5C1D"
      assert_selector ".tango .unit-message", text: "The Go did not find the Bravo. Keep the Bravo on and within a few metres, then try again."
      assert_button "Try again"
      screen "66-pair-not-found"
    end
  end

  test "a Go with no Bravo known offers to set one up, or to connect one already running skyBlip" do
    in_range a_go(bluetoothTag: 0x1b2c), a_bravo(name: "skyBlip Bravo 5C1D", bluetoothTag: 0x5c1d)
    on_the_shelf do
      connect_device
      assert_selector ".tango", text: "Have a Bravo?"
      assert_link "Set up a Bravo", href: "/briefing/setup?for=go-00012a"
      screen "67-no-bravo-known"

      click_button "Connect a Bravo running skyBlip"
      assert_button "Pair with Bravo 5C1D"
      assert_equal [ { "namePrefix" => "skyBlip Bravo" } ], page.evaluate_script("fakeFleet.asked.at(-1)")
    end
  end

  private
    def connect_both
      connect_device
      page.execute_script("fakeFleet.pick = 'Bravo 5C1D'")
      find(".fleet-connect-new").click
      assert_current_path "/briefing/devices/bravo-5c1d"
      assert_selector ".fleet-unit.is-linked", text: "Bravo 5C1D"
      find(".fleet-unit-link", text: "Go 00012A").click
      assert_current_path "/briefing/devices/go-00012a"
    end
end
