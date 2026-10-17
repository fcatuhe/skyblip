require "test_helper"
require_relative "../../../test_helpers/analytics_test_helper"

class Analytics::Visit::MeasurableTest < ActiveSupport::TestCase
  include AnalyticsTestHelper

  setup do
    clear_analytics
  end

  test "measures a relation that never joined the ahoy visits" do
    browse [ [ "/buy", 30 ], [ "/guides", 10 ] ], visitor: "prospect", started_at: 2.days.ago
    browse [ [ "/buy", 5 ] ], visitor: "prospect", started_at: 1.day.ago

    metrics = Analytics::Visit.where(segment: "non_user").metrics

    assert_equal 1, metrics[:visitors]
    assert_equal 2, metrics[:visits]
    assert_equal 3, metrics[:pageviews]
    assert_equal 0.5, metrics[:bounce_rate]
  end
end
