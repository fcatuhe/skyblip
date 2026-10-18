require "test_helper"
require_relative "../../../test_helpers/analytics_test_helper"

class Admin::Analytics::VisitorsControllerTest < ActionDispatch::IntegrationTest
  include AnalyticsTestHelper

  setup do
    clear_analytics
    sign_in_as users(:francois)
  end

  test "lists the visitors, a signed-in one by name" do
    browse [ [ "/", 30 ] ], visitor: "pilot-token", started_at: 1.hour.ago
    browse [ [ "/admin/users", 30 ] ], visitor: "francois-token", started_at: 2.hours.ago, user: users(:francois)

    get admin_analytics_visitors_path

    assert_response :success
    assert_select "a.analytics-visitor[href^=?]", admin_analytics_visitor_path("pilot-token"), text: "pilot-to"
    assert_select "a.analytics-visitor--user", text: "François Catuhe"
  end

  test "pages through the visitors, newest first" do
    51.times { |index| browse [ [ "/", 1 ] ], visitor: "visitor-#{index}", started_at: (index + 1).minutes.ago }

    get admin_analytics_visitors_path

    assert_select "a.analytics-visitor", 50
    assert_select "a[rel=next]", text: "Older"

    get admin_analytics_visitors_path(page: 2)

    assert_select "a.analytics-visitor", text: "visitor-"
    assert_select "a[rel=prev]", text: "Newer"
    assert_select "a[rel=next]", count: 0
  end

  test "shows a visitor's timeline, ten visits at a time" do
    11.times { |index| browse [ [ "/guides/#{index}", 1 ] ], visitor: "pilot", started_at: (index + 1).hours.ago }

    get admin_analytics_visitor_path("pilot")

    assert_response :success
    assert_select ".analytics-timeline strong", text: "/guides/0"
    assert_select ".analytics-timeline strong", text: "/guides/10", count: 0
    assert_select "a[rel=next][href*=?]", "page=2"

    get admin_analytics_visitor_path("pilot", page: 2)

    assert_select ".analytics-timeline strong", text: "/guides/10"
  end

  test "names a visitor whose user is gone by her number" do
    gone = User.create!(first_name: "Jean", last_name: "Mermoz", email: "jean@aeropostale.test", password: "a-long-password")
    browse [ [ "/", 30 ] ], visitor: "pilot", started_at: 1.hour.ago, user: gone
    gone.destroy!

    get admin_analytics_visitors_path
    assert_select "a.analytics-visitor--user", text: "User #{gone.id}"

    get admin_analytics_visitor_path("pilot")
    assert_select ".admin-visitor-users li", text: "User #{gone.id}"
  end
end
