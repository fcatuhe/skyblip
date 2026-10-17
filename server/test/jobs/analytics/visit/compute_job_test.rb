require "test_helper"

class Analytics::Visit::ComputeJobTest < ActiveJob::TestCase
  def setup
    Ahoy::Event.delete_all
    Ahoy::Visit.delete_all
    Analytics::Visit.delete_all
  end

  test "perform creates analytics visit record" do
    visit = Ahoy::Visit.create!(
      visit_token: "visit-1",
      visitor_token: "visitor-1",
      started_at: Time.zone.parse("2026-01-15 10:00:00"),
      landing_page: "/home"
    )

    Ahoy::Event.create!(
      name: "$view",
      time: visit.started_at + 1.minute,
      visit_id: visit.id,
      properties: { "page" => "/home" }
    )

    Analytics::Visit::ComputeJob.perform_now(visit)

    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)
    assert_not_nil analytics
    assert_equal 1, analytics.pageviews_count
    assert_equal "/home", analytics.entry_path
  end

  test "compute_later enqueues job" do
    visit = Ahoy::Visit.create!(
      visit_token: "visit-1",
      visitor_token: "visitor-1",
      started_at: Time.zone.parse("2026-01-15 10:00:00")
    )

    assert_enqueued_with(job: Analytics::Visit::ComputeJob, args: [ visit ]) do
      Analytics::Visit.compute_later(visit)
    end
  end
end
