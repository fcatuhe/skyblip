require "test_helper"
require_relative "../../../test_helpers/analytics_test_helper"

class Analytics::Visit::PurgeNonBrowserVisitsJobTest < ActiveJob::TestCase
  include AnalyticsTestHelper

  setup { clear_analytics }

  test "purges the old visits no browser ever ran, with their events, and keeps the rest" do
    crawled = Ahoy::Visit.create!(visit_token: "crawled", visitor_token: "crawler", started_at: 8.days.ago)
    Ahoy::Event.create!(name: "content/pages#show", visit_id: crawled.id, time: crawled.started_at, properties: {})
    recent = Ahoy::Visit.create!(visit_token: "recent", visitor_token: "crawler", started_at: 1.day.ago)
    browsed = browse [ [ "/", 30 ] ], visitor: "pilot", started_at: 8.days.ago

    Analytics::Visit::PurgeNonBrowserVisitsJob.perform_now

    assert_equal [ recent, browsed ].sort, Ahoy::Visit.all.sort
    assert_empty Ahoy::Event.where(visit_id: crawled.id)
  end
end
