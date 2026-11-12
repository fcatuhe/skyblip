require "test_helper"

class Briefing::ToolsControllerTest < ActionDispatch::IntegrationTest
  test "the developer tools are listed on a page of their own" do
    get briefing_tools_path

    assert_response :success
    assert_select "a[href='/briefing/console']", text: /Bluetooth console/
    assert_select "a[href='/briefing/comparison']", text: /Traffic comparison/
    assert_select "a[href='/briefing/simulator']", text: /Simulator/
    assert_select "a[href='/briefing/ownship']", text: /Ownship editor/
  end
end
