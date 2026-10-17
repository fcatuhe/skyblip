require "test_helper"

class Briefing::ConsolesControllerTest < ActionDispatch::IntegrationTest
  test "the browser console keeps serial and configuration controls in Rails" do
    get briefing_console_path

    assert_response :success
    assert_select "[data-controller=console]", count: 1
    assert_select "input#line[aria-label='NMEA sentence']", count: 1
    assert_select "input#json[aria-label='JSON command']", count: 1
    assert_select "#configpanel[hidden]", count: 1
    assert_select "a[href='#{briefing_simulator_path}']", count: 1
    assert_select "main script, main style, main [style], main [onclick], main [oninput]", count: 0
  end

  test "the console imports app-owned protocol modules through importmap" do
    get briefing_console_path

    assert_response :success
    imports = JSON.parse(css_select("script[type=importmap]").first.text).fetch("imports")
    assert_match %r{/assets/controllers/console_controller-[a-f0-9]+\.js\z}, imports.fetch("controllers/console_controller")
    assert_match %r{/assets/skyblip/ble-[a-f0-9]+\.js\z}, imports.fetch("#skyblip/ble")
  end
end
