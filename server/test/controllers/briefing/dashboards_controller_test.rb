require "test_helper"

class Briefing::DashboardsControllerTest < ActionDispatch::IntegrationTest
  test "briefing links the device tools without calling GitHub" do
    get "/briefing"

    assert_response :success
    assert_select "h1", "Briefing"
    assert_select "a[href='/briefing/simulator']"
    assert_select "a[href='/briefing/console']"
  end
end
