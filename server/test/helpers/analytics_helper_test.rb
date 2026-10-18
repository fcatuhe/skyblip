require "test_helper"

class AnalyticsHelperTest < ActionView::TestCase
  include AnalyticsHelper

  test "analytics_url_params strips blank values" do
    assert_equal({}, analytics_url_params({ start_on: "", end_on: "" }))
  end

  test "analytics_url_params keeps present filter values" do
    result = analytics_url_params({ start_on: "2026-01-01", end_on: "2026-01-31" })
    assert_equal "2026-01-01", result[:filters][:start_on]
  end

  test "display_filters excludes date and segment keys" do
    filters = { start_on: "2026-01-01", end_on: "2026-01-31", segment: "user", browser: "Chrome" }
    result = display_filters(filters)
    assert_equal({ browser: "Chrome" }, result)
  end

  test "analytics_filter_chip_label formats key-value pair" do
    assert_equal "#{analytics_filter_label(:browser)}: Chrome", analytics_filter_chip_label(:browser, "Chrome")
  end

  test "analytics_filter_chip_label shows unknown translation for unknown value" do
    label = analytics_filter_chip_label(:browser, "unknown")
    assert_includes label, I18n.t("analytics.unknown")
  end

  test "format_duration reads as a clock, then in days" do
    assert_equal "0:00", format_duration(0)
    assert_equal "1:05", format_duration(65)
    assert_equal "1:01:01", format_duration(3_661)
    assert_equal "2d 03h", format_duration(2 * 86_400 + 3 * 3_600)
  end

  test "analytics_label flags a place with its country" do
    assert_equal "\u{1F1EB}\u{1F1F7} Toulouse", analytics_label(:city, "Toulouse", country_code: "fr")
    assert_equal "Firefox", analytics_label(:browser, "Firefox")
  end
end
