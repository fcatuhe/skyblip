require "test_helper"

class Admin::InsightsControllerTest < ActionDispatch::IntegrationTest
  test "shows the numbers for the period against the one before" do
    sign_in_as users(:francois)
    Event.create! name: "device.connected", context: { device: "go-00012a", browser: "01AZ018Z" }, occurred_at: 1.day.ago

    get admin_insights_path(days: 30)

    assert_response :success
    assert_select "a[aria-current=page]", text: "30 days"
    assert_select "h2", text: "Stranded"
  end
end
