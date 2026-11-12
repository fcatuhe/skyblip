require "application_system_test_case"

class ConsoleTest < ApplicationSystemTestCase
  # The record the firmware's host suite pins, a later one of the same aircraft, a second aircraft, a traffic record and the end.
  CAPTURE = %w[
    15ffc80083886e6a5e4d3c12b80b0000edfeb004e5017a02
    1513000085886e6a3412dd00282300000000000000000000
    0d66000040f9a16a5e4d3c00c8050c00000000000104b903
    151300008d886e6a5e4d3c00ac0d00000000000000000000
    110000008e886e6a05000000000000000000000000000000
  ].join.freeze
  test "the console shows each frame as JSON with full names, and writes the JSON typed into it" do
    in_range a_go
    visit "/briefing/console"
    click_button "Connect"
    click_button "status"
    assert_selector "#replies .ok", text: '{"command":"status","flight":"ground","upload":false'

    fill_in "json", with: '{"command":"set","units":"nautical"}'
    click_button "Write"
    assert_selector "#replies .ok", text: '{"command":"acknowledgement","acknowledge":false,"pending":true,"reason":"confirm"}'
    assert_equal({ "units" => "nautical" }, page.evaluate_script("#{unit("00012A")}.staged"))

    fill_in "json", with: '{"command":"set","units":"imperial"}'
    click_button "Write"
    assert_selector "#replies .bad", text: "units has no value named imperial"
  end

  test "the console explains a reply refused as too wide for the link" do
    in_range a_go(mtu: 23)
    visit "/briefing/console"
    click_button "Connect"
    click_button "get"
    assert_selector "#replies .ok", text: '{"command":"acknowledgement","acknowledge":false,"reason":"small_mtu"}'
    assert_selector "#replies .warn", text: "this browser kept Bluetooth's smallest packet size, 20 bytes"
  end

  test "the console arms a capture on Bravo, fetches it and lists every aircraft it heard" do
    in_range a_bravo(diagnostics: [ { session: 1_785_628_800, records: CAPTURE, closed: true } ])
    visit "/briefing/console"
    click_button "Connect"
    select "full", from: "Profile"
    click_button "Arm"
    assert_selector "#capturemessage", text: "press its button twice to allow"
    page.execute_script("#{unit("3F2A")}.press()")
    assert_selector "#capturemessage.ok", text: "full capture armed"

    click_button "Fetch log"
    assert_selector "#capturemessage.ok", text: "Fetched session 1785628800"
    assert_text "Session 1785628800: 5 records, closed."
    assert_selector "#capturetypes li", count: 3
    assert_selector "#capturetypes", text: "adsb 3"
    assert_selector "a[download='diagnostics-1785628800.ndjson']", text: "Download NDJSON"
    screen "50-console-capture"
    assert_equal [ [ "3C4D5E", "-", "3500", "-", "-", "-", "2", "00:00:13 UTC" ], [ "DD1234", "-", "9000", "-", "-", "-", "1", "00:00:05 UTC" ] ], aircraft_rows

    click_button "Pressure altitude (ft)"
    click_button "Pressure altitude (ft)"
    assert_selector "th[aria-sort=descending]", text: "Pressure altitude (ft)"
    assert_equal %w[DD1234 3C4D5E], aircraft_rows.map(&:first)
  end

  test "the console words a capture the unit refused" do
    in_range a_bravo
    visit "/briefing/console"
    click_button "Connect"
    click_button "Stop"
    assert_selector "#capturemessage.bad", text: "Refused, not_capturing: no capture runs."
  end

  test "the capture panel forgets a unit once it disconnects" do
    in_range a_bravo(diagnostics: [ { session: 1_785_628_800, records: CAPTURE, closed: true } ])
    visit "/briefing/console"
    click_button "Connect"
    click_button "Fetch log"
    assert_selector "#aircraft tr", count: 2
    click_button "Disconnect"
    assert_no_selector "#capturepanel"

    click_button "Connect"
    assert_selector "#capturemessage", exact_text: "-"
    assert_no_selector "#aircraft tr"
    assert_no_link "Download NDJSON"
  end

  # The unit prompts on its lamp, and nobody presses.
  test "the capture panel gives up on a verdict that never comes" do
    in_range a_bravo
    visit "/briefing/console"
    click_button "Connect"
    page.execute_script("document.querySelector('[data-controller~=capture]').dataset.captureVerdictMsValue = 100")
    click_button "Arm"
    assert_selector "#capturemessage.bad", text: "No verdict from the unit within 0.1 s."
    assert_button "Arm", disabled: false
  end

  private
    def aircraft_rows
      all("#aircraft tr").map { |row| row.all("td").map(&:text) }
    end
end
