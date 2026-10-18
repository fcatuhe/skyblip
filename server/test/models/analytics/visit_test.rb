require "test_helper"

class Analytics::VisitTest < ActiveSupport::TestCase
  def setup
    Ahoy::Event.delete_all
    Ahoy::Visit.delete_all
    Analytics::Visit.delete_all
  end

  test "compute_now creates analytics visit with pageview metrics" do
    visit = create_visit("visit-1", "visitor-1")
    create_pageview(visit, "/home", offset: 1.minute)
    create_pageview(visit, "/about", offset: 2.minutes)

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert_equal 2, analytics.pageviews_count
    assert_equal [ "/home", "/about" ], analytics.page_paths
    assert_equal "/home", analytics.entry_path
    assert_equal "/about", analytics.exit_path
  end

  test "compute_now computes duration from first to last event" do
    visit = create_visit("visit-1", "visitor-1")
    create_pageview(visit, "/home", offset: 1.minute)
    create_pageview(visit, "/about", offset: 3.minutes)

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert_equal 120, analytics.visit_duration
  end

  test "compute_now computes visible duration with visibility events" do
    visit = create_visit("visit-1", "visitor-1")
    create_pageview(visit, "/home", offset: 1.minute)
    create_event(visit, "$visibilitychange", offset: 3.minutes, properties: { "state" => "hidden", "page" => "/home" })
    create_event(visit, "$visibilitychange", offset: 8.minutes, properties: { "state" => "visible", "page" => "/home" })
    create_event(visit, "$pagehide", offset: 10.minutes, properties: { "page" => "/home" })

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    # visible +1min→+3min (120s) + visible +8min→+10min (120s) = 240s
    assert_equal 240, analytics.visit_duration
  end

  test "compute_now sets last_event_at" do
    visit = create_visit("visit-1", "visitor-1")
    create_pageview(visit, "/home", offset: 1.minute)
    create_pageview(visit, "/about", offset: 5.minutes)

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert_in_delta (visit.started_at + 5.minutes).to_f, analytics.last_event_at.to_f, 1
  end

  test "compute_now sets segment to user when user_id present" do
    visit = create_visit("visit-1", "visitor-1", user_id: users(:amelie).id)
    create_pageview(visit, "/home", offset: 1.minute)

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert analytics.user?
  end

  # The admin's own browsing is what the overview leaves out by default.
  test "compute_now sets segment to internal for an admin" do
    visit = create_visit("visit-1", "visitor-1", user_id: users(:francois).id)
    create_pageview(visit, "/home", offset: 1.minute)

    Analytics::Visit.compute_now(visit)

    assert Analytics::Visit.find_by(ahoy_visit_id: visit.id).internal?
  end

  test "compute_now resolves segment to internal from another visit of the same visitor" do
    create_visit("visit-auth", "visitor-1", user_id: users(:francois).id)
    visit = create_visit("visit-anon", "visitor-1")
    create_pageview(visit, "/home", offset: 1.minute)

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert analytics.internal?
  end

  test "compute_now makes a browser internal once an admin signed in from it, whoever signed in first" do
    create_visit("visit-user", "visitor-1", user_id: users(:amelie).id, started_at: Time.zone.parse("2026-01-14 10:00:00"))
    create_visit("visit-admin", "visitor-1", user_id: users(:francois).id)
    visit = create_visit("visit-anon", "visitor-1", started_at: Time.zone.parse("2026-01-16 10:00:00"))
    create_pageview(visit, "/home", offset: 1.minute)

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert analytics.internal?
    assert_equal users(:amelie).id, analytics.user_id
  end

  # Ahoy keeps the first account on a visit, so the admin is only on the events tracked after the switch.
  test "compute_now makes a visit internal when an admin switched accounts within it, either way" do
    [ [ users(:amelie), users(:francois) ], [ users(:francois), users(:amelie) ] ].each_with_index do |(first, second), index|
      visit = create_visit("visit-#{index}", "visitor-#{index}", user_id: first.id)
      create_pageview(visit, "/home", offset: 1.minute)
      Ahoy::Event.create!(name: "content/pages#show", time: visit.started_at + 2.minutes, visit_id: visit.id, user_id: second.id, properties: {})

      Analytics::Visit.compute_now(visit)

      assert Analytics::Visit.find_by(ahoy_visit_id: visit.id).internal?, "#{first.first_name} then #{second.first_name}"
    end
  end

  test "compute_now keeps a visit of a deleted user in the user segment" do
    visit = create_visit("visit-1", "visitor-1", user_id: 42)
    create_pageview(visit, "/home", offset: 1.minute)

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert analytics.user?
  end

  test "compute_now sets segment to non_user when no user_id" do
    visit = create_visit("visit-1", "visitor-1")
    create_pageview(visit, "/home", offset: 1.minute)

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert analytics.non_user?
  end

  test "compute_now resolves segment to user when visitor has authenticated visit" do
    create_visit("visit-auth", "visitor-1", user_id: users(:amelie).id)
    visit = create_visit("visit-anon", "visitor-1")
    create_pageview(visit, "/home", offset: 1.minute)

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert analytics.user?
  end

  test "compute_now keeps segment non_user when visitor has no authenticated visits" do
    visit = create_visit("visit-anon", "visitor-1")
    create_pageview(visit, "/home", offset: 1.minute)

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert analytics.non_user?
  end

  test "compute_now classifies entry_section as admin for admin/ controller" do
    visit = create_visit("visit-1", "visitor-1")
    create_pageview(visit, "/admin", offset: 1.minute)
    create_event(visit, "page_view", offset: 1.minute, properties: { "controller" => "admin/analytics" })

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert analytics.section_admin?
  end

  test "compute_now classifies entry_section as public for other controllers" do
    visit = create_visit("visit-1", "visitor-1")
    create_pageview(visit, "/pages", offset: 1.minute)
    create_event(visit, "page_view", offset: 1.minute, properties: { "controller" => "pages" })

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert analytics.section_public?
  end

  test "compute_now classifies entry_section as public when no server events" do
    visit = create_visit("visit-1", "visitor-1")
    create_pageview(visit, "/home", offset: 1.minute)

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert analytics.section_public?
  end

  test "compute_now classifies entry_section as admin when any event is admin" do
    visit = create_visit("visit-1", "visitor-1")
    create_pageview(visit, "/home", offset: 1.minute)
    create_event(visit, "page_view", offset: 2.minutes, properties: { "controller" => "pages" })
    create_event(visit, "page_view", offset: 3.minutes, properties: { "controller" => "admin/analytics" })

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert analytics.section_admin?
  end

  test "compute_now classifies direct channel" do
    visit = create_visit("visit-1", "visitor-1")
    create_pageview(visit, "/home", offset: 1.minute)

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert analytics.direct?
  end

  test "compute_now classifies search channel" do
    visit = create_visit("visit-1", "visitor-1", referring_domain: "google.com")
    create_pageview(visit, "/home", offset: 1.minute)

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert analytics.search?
  end

  test "compute_now classifies social channel" do
    visit = create_visit("visit-1", "visitor-1", referring_domain: "facebook.com")
    create_pageview(visit, "/home", offset: 1.minute)

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert analytics.social?
  end

  test "compute_now classifies email channel" do
    visit = create_visit("visit-1", "visitor-1", referring_domain: "mail.google.com")
    create_pageview(visit, "/home", offset: 1.minute)

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert analytics.email?
  end

  test "compute_now classifies referral channel" do
    visit = create_visit("visit-1", "visitor-1", referring_domain: "example.com")
    create_pageview(visit, "/home", offset: 1.minute)

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert analytics.referral?
  end

  test "compute_now upserts existing record" do
    visit = create_visit("visit-1", "visitor-1")
    create_pageview(visit, "/home", offset: 1.minute)
    Analytics::Visit.compute_now(visit)

    create_pageview(visit, "/about", offset: 2.minutes)
    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert_equal 1, Analytics::Visit.where(ahoy_visit_id: visit.id).count
    assert_equal 2, analytics.pageviews_count
  end

  test "filter_by_segment scope" do
    visit_anon = create_visit("visit-1", "visitor-1")
    create_pageview(visit_anon, "/home", offset: 1.minute)
    visit_user = create_visit("visit-2", "visitor-2", user_id: 42)
    create_pageview(visit_user, "/home", offset: 1.minute)
    Analytics::Visit.compute_now(visit_anon)
    Analytics::Visit.compute_now(visit_user)

    assert_equal 1, Analytics::Visit.filter_by_segment("non_user").count
    assert_equal 1, Analytics::Visit.filter_by_segment("user").count
  end

  test "filter_by_channel scope" do
    visit_direct = create_visit("visit-1", "visitor-1")
    create_pageview(visit_direct, "/home", offset: 1.minute)
    visit_search = create_visit("visit-2", "visitor-2", referring_domain: "google.com")
    create_pageview(visit_search, "/home", offset: 1.minute)
    Analytics::Visit.compute_now(visit_direct)
    Analytics::Visit.compute_now(visit_search)

    assert_equal 1, Analytics::Visit.filter_by_channel("direct").count
    assert_equal 1, Analytics::Visit.filter_by_channel("search").count
  end

  test "filter_by_path scope uses json_each" do
    visit = create_visit("visit-1", "visitor-1")
    create_pageview(visit, "/home", offset: 1.minute)
    create_pageview(visit, "/about", offset: 2.minutes)
    Analytics::Visit.compute_now(visit)

    assert_equal 1, Analytics::Visit.filter_by_path("/home").count
    assert_equal 1, Analytics::Visit.filter_by_path("/about").count
    assert_equal 0, Analytics::Visit.filter_by_path("/pricing").count
  end

  test "filter_by_entry scope" do
    visit = create_visit("visit-1", "visitor-1")
    create_pageview(visit, "/home", offset: 1.minute)
    Analytics::Visit.compute_now(visit)

    assert_equal 1, Analytics::Visit.filter_by_entry("/home").count
    assert_equal 0, Analytics::Visit.filter_by_entry("/about").count
  end

  test "filter_by_exit scope" do
    visit = create_visit("visit-1", "visitor-1")
    create_pageview(visit, "/home", offset: 1.minute)
    create_pageview(visit, "/about", offset: 2.minutes)
    Analytics::Visit.compute_now(visit)

    assert_equal 1, Analytics::Visit.filter_by_exit("/about").count
    assert_equal 0, Analytics::Visit.filter_by_exit("/home").count
  end

  test "visit with no browser events is not computed" do
    visit = create_visit("visit-1", "visitor-1")

    Analytics::Visit.compute_now(visit)

    assert_nil Analytics::Visit.find_by(ahoy_visit_id: visit.id)
  end

  test "visit with only browser events has zero duration and empty paths" do
    visit = create_visit("visit-1", "visitor-1")
    create_pageview(visit, "/home", offset: 1.minute)

    Analytics::Visit.compute_now(visit)
    analytics = Analytics::Visit.find_by(ahoy_visit_id: visit.id)

    assert_equal 1, analytics.pageviews_count
    assert_equal 0, analytics.visit_duration
    assert_equal [ "/home" ], analytics.page_paths
    assert_equal "/home", analytics.entry_path
    assert_equal "/home", analytics.exit_path
  end

  private
    def create_visit(token, visitor_token, started_at: Time.zone.parse("2026-01-15 10:00:00"), **attrs)
      Ahoy::Visit.create!(visit_token: token, visitor_token:, started_at:, **attrs)
    end

    def create_pageview(visit, path, offset:)
      create_event(visit, "$view", offset: offset, properties: { "page" => path })
    end

    def create_event(visit, name, offset:, properties: {})
      Ahoy::Event.create!(
        name: name,
        time: visit.started_at + offset,
        visit_id: visit.id,
        properties: properties
      )
    end
end
