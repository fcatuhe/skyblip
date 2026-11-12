require "application_system_test_case"

class Briefing::ActivationTest < ApplicationSystemTestCase
  test "the address on the glass leads to activation, and an activated unit opens on its settings" do
    in_range a_new_go
    on_the_shelf do
      visit "/activate"
      assert_current_path "/briefing/activate"
      assert_text "Its glass shows ACTIVATE and a code like 3F2A."
      screen "30-activate-find"

      click_button "Find the new Go"
      assert_selector ".unit-facts", text: "Connected · 82 % · On the ground · 0.1.0+1000, development · Not activated"
      assert_selector "summary", text: "What skyblip.eu keeps"
      screen "31-activate-ready"

      click_button "Activate"
      assert_selector ".glass-title", text: "ACTIVATE"
      assert_selector ".glass-detail", text: "SEND THE KEY OF THIS UNIT TO SKYBLIP.EU"
      screen "32-activate-asking"

      page.execute_script("#{unit("3F2A skyBlip")}.press()")
      assert_current_path "/briefing/devices/go-000003?activated=1"
      assert_selector ".unit-notes .is-good", text: "Activated: skyBlip Go 000003. Its radio is on. Add your callsign and aircraft type."
      assert_no_selector ".unit-banner"
      assert_selector ".fleet-unit.is-linked", text: "Go 000003"
      assert_field "Callsign"
      assert_equal 3, Unit.find_by!(device_id: vectors["device_id"]).serial
      screen "33-activated"

      click_button "Done"
      assert_current_path "/briefing/devices/go-000003"
      assert_no_selector ".unit-notes .is-good"
    end
  end

  test "a new unit connected from the home page is taken to its activation" do
    in_range a_new_go
    on_the_shelf do
      visit "/briefing"
      click_button "Connect a device"
      assert_current_path "/briefing/activate"
      assert_button "Activate"
    end
  end

  test "a refusal on the glass is said, and a reconnect brings Activate back" do
    in_range a_new_go
    on_the_shelf do
      visit "/briefing/activate"
      click_button "Find the new Go"
      click_button "Activate"
      assert_selector ".glass-title", text: "ACTIVATE"
      page.execute_script("#{unit("3F2A skyBlip")}.refuse()")
      assert_selector ".unit-message", text: "Refused on the Go. Reconnect, then activate again."
      assert_no_button "Activate"
      screen "34-activate-refused"

      click_button "Reconnect"
      assert_button "Activate"
      assert_equal 1, page.evaluate_script("fakeFleet.asked.length")
    end
  end

  test "a phone that kept the smallest Bluetooth packet is told so before anything reaches skyblip.eu" do
    in_range a_new_go(mtu: 23)
    on_the_shelf do
      visit "/briefing/activate"
      click_button "Find the new Go"
      assert_selector ".unit-message", text: "kept Bluetooth's smallest packet"
      assert_nil Unit.find_by(device_id: vectors["device_id"])
    end
  end

  test "the French address on the glass leads to the French activation" do
    visit "/fr/activer"
    assert_current_path "/fr/briefing/activate"
    assert_selector "h1", text: "Activer un Go neuf"
  end
end
