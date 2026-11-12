require "application_system_test_case"

class ConsoleTest < ApplicationSystemTestCase
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
end
