require "test_helper"
require_relative "../../test_helpers/analytics_test_helper"

class Admin::AnalyticsControllerTest < ActionDispatch::IntegrationTest
  include AnalyticsTestHelper

  setup do
    clear_analytics
    sign_in_as users(:francois)
  end

  test "the overview counts the visitors and breaks their visits down" do
    browse [ [ "/", 30 ], [ "/buy", 10 ] ], visitor: "pilot", started_at: 1.day.ago
    browse [ [ "/guides", 5 ] ], visitor: "other-pilot", started_at: 2.days.ago

    get admin_analytics_path

    assert_response :success
    assert_select ".admin-metrics div:first-child dd a[href^=?]", admin_analytics_visitors_path, text: "2"
    assert_select ".admin-breakdown[open] summary", text: "Paths"
    assert_select ".admin-breakdown a", text: "/buy"
    assert_select ".admin-legend dt", text: "Segments"
  end

  test "the non-users segment leaves the admin's own visits out" do
    browse [ [ "/", 30 ] ], visitor: "pilot", started_at: 1.day.ago
    browse [ [ "/admin/analytics", 30 ] ], visitor: "francois", started_at: 1.day.ago, user: users(:francois)

    get admin_analytics_path(filters: { segment: "non_user" })

    assert_select ".admin-metrics div:first-child dd", text: "1"
    assert_select ".admin-pills a[aria-current][href*=?]", "filters%5Bsegment%5D=non_user", text: "Non-users"

    get admin_analytics_path

    assert_select ".admin-metrics div:first-child dd", text: "2"
  end

  # Both visits are a single pageview, so every line runs 0 to 100 on a scale of 1.
  test "the chart draws a line per series over the days in range" do
    browse [ [ "/", 0 ] ], visitor: "pilot", started_at: Time.utc(2026, 2, 4, 10)
    browse [ [ "/buy", 0 ] ], visitor: "other-pilot", started_at: Time.utc(2026, 2, 6, 9)

    get admin_analytics_path(filters: { start_on: "2026-02-01", end_on: "2026-02-10" })

    assert_select ".analytics-chart__mark", 10
    assert_select ".analytics-chart__mark title", text: "Feb 4 · 1 pageviews, 1 visits, 1 visitors"
    assert_select ".analytics-chart__line[points^=?]", "0.0,100.0 11.11,100.0 22.22,100.0 33.33,0.0", count: 3
  end

  test "keeps the filters of the bar and falls back to the last thirty days" do
    freeze_time

    get admin_analytics_path(filters: { start_on: "", end_on: "", segment: "user", browser: "Firefox" })

    assert_response :success
    assert_select ".admin-period input[name='filters[start_on]'][value=?]", 30.days.ago.to_date.iso8601
    assert_select ".admin-period input[name='filters[segment]'][value=?]", "user"
    assert_select ".admin-pills a[aria-current]", text: "30d"
    assert_select ".admin-chips a", text: /Browser: Firefox/
  end

  test "is for admins only" do
    sign_in_as users(:amelie)

    get admin_analytics_path

    assert_response :forbidden
  end
end
