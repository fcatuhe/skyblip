require "test_helper"

class Analytics::Visit::FilterTest < ActiveSupport::TestCase
  def setup
    Ahoy::Event.delete_all
    Ahoy::Visit.delete_all
    Analytics::Visit.delete_all
  end

  test "returns Analytics::Visit relation" do
    create_computed_visit("visit-1", "visitor-1", pages: [ "/home" ])

    result = filtered

    assert_kind_of ActiveRecord::Relation, result
    assert_equal Analytics::Visit, result.model
  end

  # --- Date filtering ---

  test "date range narrows visits" do
    create_computed_visit("visit-1", "visitor-1", started_at: Time.zone.parse("2026-01-10 10:00:00"), pages: [ "/home" ])
    create_computed_visit("visit-2", "visitor-2", started_at: Time.zone.parse("2026-01-20 10:00:00"), pages: [ "/home" ])

    assert_equal 1, filtered(start_on: Date.parse("2026-01-15")).count
  end

  # --- Ahoy filters ---

  test "browser filter" do
    create_computed_visit("visit-1", "visitor-1", browser: "Chrome", pages: [ "/home" ])
    create_computed_visit("visit-2", "visitor-2", browser: "Firefox", pages: [ "/home" ])

    assert_equal 1, filtered(browser: "Chrome").count
  end

  test "country filter" do
    create_computed_visit("visit-1", "visitor-1", country: "France", pages: [ "/home" ])
    create_computed_visit("visit-2", "visitor-2", country: "Germany", pages: [ "/home" ])

    assert_equal 1, filtered(country: "France").count
  end

  # --- Analytics filters ---

  test "entry filter" do
    create_computed_visit("visit-1", "visitor-1", pages: [ "/home", "/about" ])
    create_computed_visit("visit-2", "visitor-2", pages: [ "/pricing" ])

    assert_equal 1, filtered(entry: "/home").count
  end

  # --- Unknown filters ---

  test "unknown entry matches visits with nil or blank entry path" do
    create_computed_visit("visit-1", "visitor-1", pages: [ "/home" ])
    create_computed_visit("visit-2", "visitor-2", events: [ "$pagehide" ])

    assert_equal 1, filtered(entry: "unknown").count
  end

  test "unknown exit matches visits with nil or blank exit path" do
    create_computed_visit("visit-1", "visitor-1", pages: [ "/home" ])
    create_computed_visit("visit-2", "visitor-2", events: [ "$pagehide" ])

    assert_equal 1, filtered(exit: "unknown").count
  end

  test "unknown path matches visits with no page paths" do
    create_computed_visit("visit-1", "visitor-1", pages: [ "/home" ])
    create_computed_visit("visit-2", "visitor-2", events: [ "$pagehide" ])

    assert_equal 1, filtered(path: "unknown").count
  end

  # --- Combined filters ---

  test "ahoy and analytics filters combine" do
    create_computed_visit("visit-1", "visitor-1", browser: "Chrome", pages: [ "/home" ])
    create_computed_visit("visit-2", "visitor-2", browser: "Firefox", pages: [ "/pricing" ])

    assert_equal 1, filtered(browser: "Chrome", entry: "/home").count
    assert_equal 0, filtered(browser: "Chrome", entry: "/pricing").count
  end

  private
    def filtered(**params)
      defaults = { start_on: Date.parse("2026-01-01"), end_on: Date.parse("2026-01-31") }
      Analytics::Visit.filtered(defaults.merge(params))
    end

    def create_computed_visit(token, visitor_token, pages: [], events: [], **attrs)
      visit = Ahoy::Visit.create!(
        visit_token: token,
        visitor_token: visitor_token,
        started_at: attrs.delete(:started_at) || Time.zone.parse("2026-01-15 10:00:00"),
        **attrs
      )

      offset = 1.minute
      pages.each do |path|
        Ahoy::Event.create!(name: "$view", time: visit.started_at + offset, visit_id: visit.id, properties: { "page" => path })
        offset += 1.minute
      end

      events.each do |name|
        Ahoy::Event.create!(name: name, time: visit.started_at + offset, visit_id: visit.id, properties: { "page" => "/home" })
        offset += 1.minute
      end

      Analytics::Visit.compute_now(visit)
    end
end
