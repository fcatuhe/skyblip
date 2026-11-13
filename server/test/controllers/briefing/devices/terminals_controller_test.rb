require "test_helper"

class Briefing::Devices::TerminalsControllerTest < ActionDispatch::IntegrationTest
  test "the terminal page offers a filter per endpoint and an empty tape, linked from its device page" do
    get briefing_device_terminal_path(device_slug: "go-00012a")

    assert_response :success
    assert_select "h1", text: "Terminal"
    assert_select "a.unit-back[href='/briefing/devices/go-00012a']"
    assert_equal %w[nmea config log smp], css_select(".terminal-endpoints input[type=checkbox]").map { |input| input["value"] }
    assert_select ".terminal-tape[role=log]:empty"
    assert_select "main script, main style, main [style], main [onclick]", count: 0

    get briefing_device_path(slug: "go-00012a")
    assert_select "a.unit-row[href='/briefing/devices/go-00012a/terminal']"
  end
end
