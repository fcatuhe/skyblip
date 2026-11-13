require "application_system_test_case"

class Briefing::TerminalTest < ApplicationSystemTestCase
  test "the terminal shows every endpoint of a connected device, and follows it through a reconnect" do
    in_range a_go(mtu: 498)
    on_the_shelf do
      connect_go
      click_link "Terminal"
      assert_current_path "/briefing/devices/go-00012a/terminal"

      page.execute_script("#{unit("00012A")}.sentence('$PFLAU,0,1,1,1,0,,0,,*63')")
      page.execute_script("#{unit("00012A")}.status()")
      assert_selector ".terminal-line.is-nmea.is-in", text: "nmea   < $PFLAU,0,1,1,1,0,,0,,*63"
      assert_selector ".terminal-line.is-config.is-in", text: 'config < {"command":"status","flight":"ground"'
      screen "40-terminal"

      uncheck "nmea"
      assert_no_selector ".terminal-line.is-nmea"
      assert_selector ".terminal-line.is-config"

      find(".unit-menu summary").click
      click_button "Disconnect"
      assert_selector ".terminal-line.is-event", text: "-- link closed"

      within(".unit-offline") { click_button "Connect" }
      assert_selector ".terminal-line.is-event", text: "-- link open"
      assert_selector ".terminal-line.is-config.is-out", text: 'config > {"command":"update"}'
      assert_selector ".terminal-line.is-config.is-in", text: 'config < {"command":"update","image":"confirmed"'
      assert_selector ".terminal-line.is-smp.is-out", text: "smp    > read os params seq"
      assert_selector ".terminal-line.is-smp.is-in", text: /read reply os params seq \d+ \{"buf_size":2475,"buf_count":4\}/
    end
  end
end
