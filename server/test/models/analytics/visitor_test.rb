require "test_helper"

class Analytics::VisitorTest < ActiveSupport::TestCase
  def setup
    Ahoy::Event.delete_all
    Ahoy::Visit.delete_all
    Analytics::Visit.delete_all

    @visit_one = Ahoy::Visit.create!(
      visit_token: "visit-one",
      visitor_token: "visitor-1",
      user_id: 10,
      started_at: Time.zone.parse("2026-02-01 10:00:00"),
      landing_page: "/home",
      referring_domain: "google.com",
      country: "France",
      region: "Ile-de-France",
      city: "Paris",
      browser: "Chrome",
      os: "macOS",
      device_type: "Desktop"
    )

    @visit_two = Ahoy::Visit.create!(
      visit_token: "visit-two",
      visitor_token: "visitor-1",
      user_id: 10,
      started_at: Time.zone.parse("2026-02-02 11:00:00"),
      landing_page: "/pricing",
      referrer: "https://example.com",
      country: "France",
      region: "Auvergne-Rhone-Alpes",
      city: "Lyon",
      browser: "Firefox",
      os: "Windows",
      device_type: "Desktop"
    )

    Ahoy::Event.create!(
      name: "$view",
      time: @visit_one.started_at + 1.minute,
      visit_id: @visit_one.id,
      properties: { "page" => "/home", "utm_campaign" => "winter" }
    )
    Ahoy::Event.create!(
      name: "signup",
      time: @visit_one.started_at + 2.minutes,
      visit_id: @visit_one.id,
      properties: { "plan" => "pro" }
    )
    Ahoy::Event.create!(
      name: "$view",
      time: @visit_two.started_at + 1.minute,
      visit_id: @visit_two.id,
      properties: { "page" => "/pricing" }
    )

    compute_all
  end

  # --- Metrics ---

  test "metrics include visits views events and duration" do
    visitor = Analytics::Visitor.new(visitor_token: "visitor-1")
    metrics = visitor.metrics

    assert_equal 2, metrics[:visits]
    assert_equal 2, metrics[:views]
    assert_equal 1, metrics[:events]
    assert_equal 60, metrics[:visit_duration]
  end

  test "pagehide computes visible duration from first event" do
    Ahoy::Event.create!(
      name: "$pagehide",
      time: @visit_one.started_at + 5.minutes,
      visit_id: @visit_one.id,
      properties: { "page" => "/home" }
    )
    compute_all

    visitor = Analytics::Visitor.new(visitor_token: "visitor-1")

    assert_equal 240, visitor.metrics[:visit_duration]
  end

  # --- Metadata ---

  test "metadata reflects latest visit" do
    visitor = Analytics::Visitor.new(visitor_token: "visitor-1")
    metadata = visitor.metadata

    assert_equal [ 10 ], metadata[:all_user_ids]
    assert_equal "google.com", metadata[:referrer]
    assert_equal "France", metadata[:country]
    assert_equal "Lyon", metadata[:city]
    assert_equal "Firefox", metadata[:browser]
    assert_equal "Windows", metadata[:os]
  end

  test "metadata referrer defaults to Direct" do
    @visit_one.update!(referring_domain: nil)
    visitor = Analytics::Visitor.new(visitor_token: "visitor-1")

    assert_equal "Direct", visitor.metadata[:referrer]
  end

  test "metadata names every user the visitor signed in as" do
    auth_first = Ahoy::Visit.create!(visit_token: "visit-auth-first", visitor_token: "visitor-mixed", user_id: 30, started_at: Time.zone.parse("2026-02-01 09:00:00"))
    auth_second = Ahoy::Visit.create!(visit_token: "visit-auth-second", visitor_token: "visitor-mixed", user_id: 20, started_at: Time.zone.parse("2026-02-01 12:00:00"))
    Ahoy::Event.create!(name: "$view", time: auth_first.started_at + 1.minute, visit_id: auth_first.id, properties: { "page" => "/home" })
    Ahoy::Event.create!(name: "$view", time: auth_second.started_at + 1.minute, visit_id: auth_second.id, properties: { "page" => "/dashboard" })
    compute_all

    visitor = Analytics::Visitor.new(visitor_token: "visitor-mixed")

    assert_equal [ 20, 30 ], visitor.metadata[:all_user_ids].sort
  end

  test "all visits are included regardless of date" do
    outside_visit = Ahoy::Visit.create!(visit_token: "visit-outside", visitor_token: "visitor-1", started_at: Time.zone.parse("2026-02-10 10:00:00"), landing_page: "/late")
    Ahoy::Event.create!(name: "$view", time: outside_visit.started_at + 1.minute, visit_id: outside_visit.id, properties: { "page" => "/late" })
    compute_all

    assert_equal 3, Analytics::Visitor.new(visitor_token: "visitor-1").metrics[:visits]
  end

  # --- Source validation ---

  test "invalid source falls back to browser" do
    visitor = Analytics::Visitor.new(visitor_token: "visitor-1", source: "bogus")

    assert_equal "browser", visitor.source
  end

  # --- Timeline (delegated) ---

  test "timeline includes events with type and properties" do
    visitor = Analytics::Visitor.new(visitor_token: "visitor-1", source: "all")
    entries = visitor.timeline.groups.flat_map { it[:events] }
    event_types = entries.map(&:type)

    assert_includes event_types, "$view"
    assert_includes event_types, "signup"
  end

  test "timeline browser source excludes server events" do
    entries = Analytics::Visitor.new(visitor_token: "visitor-1").timeline.groups.flat_map { it[:events] }

    assert entries.none? { it.type == "signup" }
  end

  test "timeline server source excludes browser events" do
    entries = Analytics::Visitor.new(visitor_token: "visitor-1", source: "server").timeline.groups.flat_map { it[:events] }

    assert entries.none? { it.type.to_s.start_with?("$") }
    assert entries.any? { it.type == "signup" }
  end

  test "timeline includes session start and end markers" do
    entries = Analytics::Visitor.new(visitor_token: "visitor-1", source: "all").timeline.groups.flat_map { it[:events] }
    types = entries.map(&:type)

    assert_includes types, :session_start
    assert_includes types, :session_end
  end

  test "timeline groups events by date" do
    visitor = Analytics::Visitor.new(visitor_token: "visitor-1", source: "all")
    dates = visitor.timeline.groups.map { it[:date] }

    assert_includes dates, Date.parse("2026-02-01")
    assert_includes dates, Date.parse("2026-02-02")
  end

  test "duration computation with visibility events" do
    Ahoy::Event.create!(name: "$visibilitychange", time: @visit_one.started_at + 3.minutes, visit_id: @visit_one.id, properties: { "page" => "/home", "state" => "hidden" })
    Ahoy::Event.create!(name: "$visibilitychange", time: @visit_one.started_at + 5.minutes, visit_id: @visit_one.id, properties: { "page" => "/home", "state" => "visible" })
    Ahoy::Event.create!(name: "$pagehide", time: @visit_one.started_at + 7.minutes, visit_id: @visit_one.id, properties: { "page" => "/home" })
    compute_all

    assert_equal 240, Analytics::Visitor.new(visitor_token: "visitor-1").metrics[:visit_duration]
  end

  private
    def compute_all
      Ahoy::Visit.find_each { |v| Analytics::Visit.compute_now(v) }
    end
end
