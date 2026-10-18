require "test_helper"

class Analytics::Visitor::TimelineTest < ActiveSupport::TestCase
  def setup
    Ahoy::Event.delete_all
    Ahoy::Visit.delete_all
    Analytics::Visit.delete_all
  end

  test "first page shows only the first page of visits" do
    visit_day1 = create_visit("visit-d1", "visitor-pag", started_at: Time.zone.parse("2026-03-02 10:00:00"))
    3.times { |i| create_pageview(visit_day1, "/page-#{i}", offset: (i + 1).minutes) }

    visit_day2 = create_visit("visit-d2", "visitor-pag", started_at: Time.zone.parse("2026-03-01 10:00:00"))
    3.times { |i| create_pageview(visit_day2, "/older-#{i}", offset: (i + 1).minutes) }

    compute_all

    timeline = Analytics::Visitor::Timeline.new(
      visitor_token: "visitor-pag", source: "browser", page: 1, per_page: 1
    )
    groups = timeline.groups

    assert_equal 1, groups.size, "First page should only show 1 date group"
    assert_equal Date.parse("2026-03-02"), groups.first[:date]
    assert timeline.more?, "Should indicate more pages available"
  end

  test "second page shows the remaining visits" do
    visit_day1 = create_visit("visit-d1", "visitor-pag", started_at: Time.zone.parse("2026-03-02 10:00:00"))
    3.times { |i| create_pageview(visit_day1, "/page-#{i}", offset: (i + 1).minutes) }

    visit_day2 = create_visit("visit-d2", "visitor-pag", started_at: Time.zone.parse("2026-03-01 10:00:00"))
    3.times { |i| create_pageview(visit_day2, "/older-#{i}", offset: (i + 1).minutes) }

    compute_all

    timeline = Analytics::Visitor::Timeline.new(
      visitor_token: "visitor-pag", source: "browser", page: 2, per_page: 1
    )
    groups = timeline.groups

    assert_equal 1, groups.size, "Second page should show the other date group"
    assert_equal Date.parse("2026-03-01"), groups.first[:date]
    assert_not timeline.more?, "Should not indicate more pages"
  end

  test "large per_page returns all visits in one page" do
    visit_day1 = create_visit("visit-d1", "visitor-pag", started_at: Time.zone.parse("2026-03-02 10:00:00"))
    3.times { |i| create_pageview(visit_day1, "/page-#{i}", offset: (i + 1).minutes) }

    visit_day2 = create_visit("visit-d2", "visitor-pag", started_at: Time.zone.parse("2026-03-01 10:00:00"))
    3.times { |i| create_pageview(visit_day2, "/older-#{i}", offset: (i + 1).minutes) }

    compute_all

    timeline = Analytics::Visitor::Timeline.new(
      visitor_token: "visitor-pag", source: "browser", page: 1, per_page: 100
    )
    groups = timeline.groups

    assert_equal 2, groups.size
    assert_not timeline.more?
  end

  test "session markers are included for each visit" do
    visit = create_visit("visit-1", "visitor-markers", started_at: Time.zone.parse("2026-03-02 10:00:00"))
    create_pageview(visit, "/home", offset: 1.minute)
    compute_all

    timeline = Analytics::Visitor::Timeline.new(
      visitor_token: "visitor-markers", source: "browser", page: 1
    )
    entries = timeline.groups.flat_map { it[:events] }
    types = entries.map(&:type)

    assert_includes types, :session_start
    assert_includes types, :session_end
  end

  test "source filtering works within paginated visits" do
    visit = create_visit("visit-1", "visitor-src", started_at: Time.zone.parse("2026-03-02 10:00:00"))
    create_pageview(visit, "/home", offset: 1.minute)
    create_event(visit, "signup", offset: 2.minutes, properties: { "plan" => "pro" })
    compute_all

    browser_timeline = Analytics::Visitor::Timeline.new(
      visitor_token: "visitor-src", source: "browser", page: 1
    )
    browser_entries = browser_timeline.groups.flat_map { it[:events] }
    assert browser_entries.none? { it.type == "signup" }

    server_timeline = Analytics::Visitor::Timeline.new(
      visitor_token: "visitor-src", source: "server", page: 1
    )
    server_entries = server_timeline.groups.flat_map { it[:events] }
    assert server_entries.any? { it.type == "signup" }
    assert server_entries.none? { it.type == "$view" }
  end

  private

    def create_visit(token, visitor_token, started_at:)
      Ahoy::Visit.create!(visit_token: token, visitor_token: visitor_token, started_at: started_at)
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

    def compute_all
      Ahoy::Visit.find_each { |v| Analytics::Visit.compute_now(v) }
    end
end
