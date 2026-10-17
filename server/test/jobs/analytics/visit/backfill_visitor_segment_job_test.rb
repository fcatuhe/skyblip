require "test_helper"

class Analytics::Visit::BackfillVisitorSegmentJobTest < ActiveJob::TestCase
  def setup
    Ahoy::Event.delete_all
    Ahoy::Visit.delete_all
    Analytics::Visit.delete_all
  end

  test "recomputes non_user visits to user when visitor has authenticated visits" do
    anon_visit = create_visit("visit-anon", "visitor-1")
    create_pageview(anon_visit, "/home", offset: 1.minute)
    Analytics::Visit.compute_now(anon_visit)

    assert Analytics::Visit.find_by(ahoy_visit_id: anon_visit.id).non_user?

    auth_visit = create_visit("visit-auth", "visitor-1", user_id: 42)
    create_pageview(auth_visit, "/home", offset: 1.minute)
    Analytics::Visit.compute_now(auth_visit)

    perform_enqueued_jobs do
      Analytics::Visit::BackfillVisitorSegmentJob.perform_now("visitor-1")
    end

    assert Analytics::Visit.find_by(ahoy_visit_id: anon_visit.id).user?
    assert Analytics::Visit.find_by(ahoy_visit_id: auth_visit.id).user?
  end

  test "no-op when visitor has no authenticated visits" do
    anon_visit = create_visit("visit-anon", "visitor-1")
    create_pageview(anon_visit, "/home", offset: 1.minute)
    Analytics::Visit.compute_now(anon_visit)

    perform_enqueued_jobs do
      Analytics::Visit::BackfillVisitorSegmentJob.perform_now("visitor-1")
    end

    assert Analytics::Visit.find_by(ahoy_visit_id: anon_visit.id).non_user?
  end

  private
    def create_visit(token, visitor_token, **attrs)
      Ahoy::Visit.create!(
        visit_token: token,
        visitor_token: visitor_token,
        started_at: Time.zone.parse("2026-01-15 10:00:00"),
        **attrs
      )
    end

    def create_pageview(visit, path, offset:)
      Ahoy::Event.create!(
        name: "$view",
        time: visit.started_at + offset,
        visit_id: visit.id,
        properties: { "page" => path }
      )
    end
end
