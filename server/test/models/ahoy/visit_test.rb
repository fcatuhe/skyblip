require "test_helper"
require_relative "../../test_helpers/analytics_test_helper"

class Ahoy::VisitTest < ActiveSupport::TestCase
  include AnalyticsTestHelper

  setup { clear_analytics }

  # The CNIL keeps what an exempt audience measurement collects 25 months at most.
  test "pruning deletes the visits older than 25 months, with their events and what was computed of them, and keeps the rest" do
    expired = browse [ [ "/", 30 ] ], visitor: "pilot", started_at: 25.months.ago - 1.day
    kept = browse [ [ "/", 30 ] ], visitor: "pilot", started_at: 25.months.ago + 1.day

    Ahoy::Visit.prune

    assert_equal [ kept ], Ahoy::Visit.all
    assert_empty Ahoy::Event.where(visit_id: expired.id)
    assert_equal [ kept.id ], Analytics::Visit.pluck(:ahoy_visit_id)
  end
end
