require "application_system_test_case"

class ConsoleTest < ApplicationSystemTestCase
  setup do
    script = Rails.root.join("test/system/fake_skyblip.js").read
    page.driver.browser.page.command("Page.addScriptToEvaluateOnNewDocument", source: script)
  end

  test "the console shows each frame as JSON with full names, and writes the JSON typed into it" do
    visit "/briefing/console"
    click_button "Connect"
    click_button "status"
    assert_selector "#replies .ok", text: '{"command":"status","flight":"ground","upload":false'

    fill_in "json", with: '{"command":"set","units":"nautical"}'
    click_button "Write"
    assert_selector "#replies .ok", text: '{"command":"acknowledgement","acknowledge":false,"pending":true,"reason":"confirm"}'
    assert_equal({ "units" => "nautical" }, page.evaluate_script("fakeSkyblip.staged"))

    fill_in "json", with: '{"command":"set","units":"imperial"}'
    click_button "Write"
    assert_selector "#replies .bad", text: "units has no value named imperial"
  end

  test "the console explains a reply refused as too wide for the link" do
    visit "/briefing/console"
    page.execute_script("fakeSkyblip.mtu = 23")
    click_button "Connect"
    click_button "get"
    assert_selector "#replies .ok", text: '{"command":"acknowledgement","acknowledge":false,"reason":"small_mtu"}'
    assert_selector "#replies .warn", text: "this browser kept Bluetooth's smallest packet size, 20 bytes"
  end
end
