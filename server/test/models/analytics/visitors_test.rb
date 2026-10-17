require "test_helper"

class Analytics::VisitorsTest < ActiveSupport::TestCase
  def setup
    Ahoy::Event.delete_all
    Ahoy::Visit.delete_all
    Analytics::Visit.delete_all

    @start_on = Date.parse("2026-02-01 00:00:00")
    @end_on = Date.parse("2026-02-02 23:59:59")

    @visit_one = Ahoy::Visit.create!(
      visit_token: "visit-one",
      visitor_token: "visitor-1",
      started_at: Time.zone.parse("2026-02-01 10:00:00"),
      landing_page: "/home",
      referring_domain: "google.com",
      country: "France",
      city: "Paris",
      browser: "Chrome",
      os: "macOS",
      device_type: "Desktop"
    )

    @visit_two = Ahoy::Visit.create!(
      visit_token: "visit-two",
      visitor_token: "visitor-2",
      user_id: 42,
      started_at: Time.zone.parse("2026-02-02 11:00:00"),
      landing_page: "/pricing",
      country: "Germany",
      city: "Berlin",
      browser: "Firefox",
      os: "Windows",
      device_type: "Mobile"
    )

    Ahoy::Event.create!(
      name: "$view",
      time: @visit_one.started_at + 1.minute,
      visit_id: @visit_one.id,
      properties: { "page" => "/home" }
    )
    Ahoy::Event.create!(
      name: "$view",
      time: @visit_one.started_at + 2.minutes,
      visit_id: @visit_one.id,
      properties: { "page" => "/about" }
    )
    Ahoy::Event.create!(
      name: "$view",
      time: @visit_two.started_at + 1.minute,
      visit_id: @visit_two.id,
      properties: { "page" => "/pricing" }
    )

    compute_all
  end

  test "visitors returns summaries sorted by last seen" do
    visitors = Analytics::Visitors.new(filter_params: { start_on: @start_on, end_on: @end_on })
    result = visitors.page(1).summaries

    assert_equal 2, result.size
    assert_equal "visitor-2", result.first.visitor_token
    assert_equal "visitor-1", result.second.visitor_token
  end

  test "visitors summary includes correct counts" do
    visitors = Analytics::Visitors.new(filter_params: { start_on: @start_on, end_on: @end_on })
    visitor_1 = visitors.page(1).summaries.find { |v| v.visitor_token == "visitor-1" }

    assert_equal 1, visitor_1.visits_count
    assert_equal 2, visitor_1.views_count
    assert_equal 2, visitor_1.latest_views_count
    assert_equal "Chrome", visitor_1.browser
  end

  test "segment user filter shows only authenticated visitors" do
    visitors = Analytics::Visitors.new(filter_params: { start_on: @start_on, end_on: @end_on, segment: "user" })
    result = visitors.page(1).summaries

    assert_equal 1, result.size
    assert_equal "visitor-2", result.first.visitor_token
  end

  test "segment non_user filter shows only non-authenticated visitors" do
    visitors = Analytics::Visitors.new(filter_params: { start_on: @start_on, end_on: @end_on, segment: "non_user" })
    result = visitors.page(1).summaries

    assert_equal 1, result.size
    assert_equal "visitor-1", result.first.visitor_token
  end

  test "no segment filter includes all visitors" do
    visitors = Analytics::Visitors.new(filter_params: { start_on: @start_on, end_on: @end_on })
    result = visitors.page(1).summaries

    assert_equal 2, result.size
  end

  test "browser filter narrows results" do
    visitors = Analytics::Visitors.new(filter_params: { start_on: @start_on, end_on: @end_on, browser: "Chrome" })
    result = visitors.page(1).summaries

    assert_equal 1, result.size
    assert_equal "visitor-1", result.first.visitor_token
  end

  test "multiple filters combine additively" do
    visitors = Analytics::Visitors.new(filter_params: {
      start_on: @start_on,
      end_on: @end_on,
      country: "France",
      browser: "Chrome"
    })
    result = visitors.page(1).summaries

    assert_equal 1, result.size
    assert_equal "visitor-1", result.first.visitor_token
  end

  test "contradictory filters return empty results" do
    visitors = Analytics::Visitors.new(filter_params: {
      start_on: @start_on,
      end_on: @end_on,
      country: "France",
      browser: "Firefox"
    })
    result = visitors.page(1).summaries

    assert_empty result
  end

  test "latest session fields come from the most recent visit, totals from every visit of the visitor" do
    old_visit = Ahoy::Visit.create!(
      visit_token: "visit-latest-old",
      visitor_token: "visitor-latest",
      started_at: Time.zone.parse("2026-02-01 10:00:00"),
      country: "France",
      city: "Zurich",
      browser: "Chrome",
      os: "Windows"
    )
    Ahoy::Event.create!(
      name: "$view",
      time: old_visit.started_at + 1.minute,
      visit_id: old_visit.id,
      properties: { "page" => "/zzz" }
    )
    Ahoy::Event.create!(
      name: "$view",
      time: old_visit.started_at + 2.minutes,
      visit_id: old_visit.id,
      properties: { "page" => "/yyy" }
    )

    new_visit = Ahoy::Visit.create!(
      visit_token: "visit-latest-new",
      visitor_token: "visitor-latest",
      started_at: Time.zone.parse("2026-02-02 10:00:00"),
      country: "Germany",
      city: "Berlin",
      browser: "Safari",
      os: "iOS"
    )
    Ahoy::Event.create!(
      name: "$view",
      time: new_visit.started_at + 1.minute,
      visit_id: new_visit.id,
      properties: { "page" => "/aaa" }
    )

    compute_all

    summary = Analytics::Visitors.new(filter_params: { start_on: @start_on, end_on: @end_on })
      .page(1).summaries
      .find { |visitor| visitor.visitor_token == "visitor-latest" }

    assert_equal [ 1, 0 ], [ summary.latest_views_count, summary.latest_visit_duration ]
    assert_equal [ 2, 3, 60 ], [ summary.visits_count, summary.views_count, summary.visit_duration ]
    assert_equal "Safari", summary.browser
    assert_equal "iOS", summary.os
    assert_equal "/aaa", summary.last_entry_path
  end

  test "user_id is resolved even when authenticated visit is outside the time window" do
    # Visitor authenticated in January (outside the Feb 1-2 window)
    Ahoy::Visit.create!(
      visit_token: "visit-auth-old",
      visitor_token: "visitor-1",
      user_id: 99,
      started_at: Time.zone.parse("2026-01-15 10:00:00"),
      country: "France",
      browser: "Chrome",
      os: "macOS"
    )

    # Recompute propagates user_id to all analytics visits for this visitor
    compute_all

    visitors = Analytics::Visitors.new(filter_params: { start_on: @start_on, end_on: @end_on })
    visitor_1 = visitors.page(1).summaries.find { |v| v.visitor_token == "visitor-1" }

    assert_equal 99, visitor_1.user_id
  end

  private
    def compute_all
      Ahoy::Visit.find_each { |v| Analytics::Visit.compute_now(v) }
    end
end
