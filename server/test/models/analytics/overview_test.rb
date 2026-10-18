require "test_helper"

class Analytics::OverviewTest < ActiveSupport::TestCase
  def setup
    Ahoy::Event.delete_all
    Ahoy::Visit.delete_all
    Analytics::Visit.delete_all

    @start_on = Date.parse("2026-01-01 00:00:00")
    @end_on = Date.parse("2026-01-31 23:59:59")

    @visit_one = create_visit("visit-one", "visitor-1",
      started_at: Time.zone.parse("2026-01-10 10:00:00"),
      landing_page: "/home",
      referring_domain: "google.com")

    @visit_two = create_visit("visit-two", "visitor-2",
      started_at: Time.zone.parse("2026-01-20 11:00:00"),
      landing_page: "/pricing")

    create_pageview(@visit_one, "/home", offset: 1.minute)
    create_pageview(@visit_one, "/about", offset: 2.minutes)
    create_pageview(@visit_two, "/pricing", offset: 2.minutes)

    compute_all
  end

  test "metrics include visitors, visits, pageviews, bounce rate, and duration" do
    overview = Analytics::Overview.new(filter_params: { start_on: @start_on, end_on: @end_on })
    metrics = overview.metrics

    assert_equal 2, metrics[:visitors]
    assert_equal 2, metrics[:visits]
    assert_equal 3, metrics[:pageviews]
    assert_in_delta 0.5, metrics[:bounce_rate], 0.001
    assert_equal 30, metrics[:visit_duration]
  end

  test "metrics of a period without visits are zeros" do
    overview = Analytics::Overview.new(filter_params: { start_on: @end_on + 1, end_on: @end_on + 2 })

    assert_equal({ visitors: 0, visits: 0, pageviews: 0, bounce_rate: 0, visit_duration: 0 }, overview.metrics)
  end

  test "path filter narrows visits and pageviews" do
    overview = Analytics::Overview.new(filter_params: {
      start_on: @start_on,
      end_on: @end_on,
      path: "/pricing"
    })
    metrics = overview.metrics

    assert_equal 1, metrics[:visitors]
    assert_equal 1, metrics[:visits]
    assert_equal 1, metrics[:pageviews]
    assert_in_delta 1.0, metrics[:bounce_rate], 0.001
  end

  test "visibility events affect precomputed duration" do
    create_event(@visit_one, "$visibilitychange", offset: 3.minutes, properties: { "page" => "/about", "state" => "hidden" })
    create_event(@visit_one, "$visibilitychange", offset: 8.minutes, properties: { "page" => "/about", "state" => "visible" })
    create_event(@visit_one, "$pagehide", offset: 10.minutes, properties: { "page" => "/about" })
    compute_all

    overview = Analytics::Overview.new(filter_params: { start_on: @start_on, end_on: @end_on })

    # visit_one: visible +1min→+3min (120s) + visible +8min→+10min (120s) = 240s
    # visit_two: legacy, single event = 0s
    # average = (240 + 0) / 2 = 120
    assert_equal 120, overview.metrics[:visit_duration]
  end

  test "segment user filter shows only authenticated visits" do
    @visit_one.update!(user_id: 42)
    compute_all

    overview = Analytics::Overview.new(filter_params: { start_on: @start_on, end_on: @end_on, segment: "user" })
    metrics = overview.metrics

    assert_equal 1, metrics[:visitors]
    assert_equal 1, metrics[:visits]
    assert_equal 2, metrics[:pageviews]
  end

  test "segment non_user filter shows only non-authenticated visits" do
    @visit_one.update!(user_id: 42)
    compute_all

    overview = Analytics::Overview.new(filter_params: { start_on: @start_on, end_on: @end_on, segment: "non_user" })
    metrics = overview.metrics

    assert_equal 1, metrics[:visitors]
    assert_equal 1, metrics[:visits]
    assert_equal 1, metrics[:pageviews]
  end

  test "no segment filter includes all visits" do
    @visit_one.update!(user_id: 42)
    compute_all

    overview = Analytics::Overview.new(filter_params: { start_on: @start_on, end_on: @end_on })
    metrics = overview.metrics

    assert_equal 2, metrics[:visitors]
    assert_equal 2, metrics[:visits]
  end

  test "entry and exit breakdowns include landing page and last pageview" do
    overview = Analytics::Overview.new(filter_params: { start_on: @start_on, end_on: @end_on })
    breakdowns = overview.breakdowns

    entry_labels = breakdowns[:entry].map { |row| row[:label] }
    exit_labels = breakdowns[:exit].map { |row| row[:label] }

    assert_includes entry_labels, "/home"
    assert_includes entry_labels, "/pricing"
    assert_includes exit_labels, "/about"
    assert_includes exit_labels, "/pricing"
  end

  test "multiple filters combine additively" do
    @visit_one.update!(browser: "Chrome", country: "France")
    @visit_two.update!(browser: "Firefox", country: "France")
    compute_all

    overview = Analytics::Overview.new(filter_params: {
      start_on: @start_on,
      end_on: @end_on,
      browser: "Chrome",
      country: "France"
    })
    metrics = overview.metrics

    assert_equal 1, metrics[:visitors]
    assert_equal 1, metrics[:visits]
    assert_equal 2, metrics[:pageviews]
  end

  test "visits spanning time range boundary are excluded" do
    create_visit("visit-before", "visitor-before",
      started_at: Time.zone.parse("2025-12-31 23:00:00"),
      landing_page: "/old")
    create_visit("visit-after", "visitor-after",
      started_at: Time.zone.parse("2026-02-01 01:00:00"),
      landing_page: "/future")
    compute_all

    overview = Analytics::Overview.new(filter_params: { start_on: @start_on, end_on: @end_on })
    metrics = overview.metrics

    assert_equal 2, metrics[:visitors]
    assert_equal 2, metrics[:visits]
  end

  test "referrer filter narrows visits via SQL" do
    @visit_one.update!(referring_domain: "google.com")
    @visit_two.update!(referring_domain: "facebook.com")
    compute_all

    overview = Analytics::Overview.new(filter_params: {
      start_on: @start_on,
      end_on: @end_on,
      referrer: "google.com"
    })
    metrics = overview.metrics

    assert_equal 1, metrics[:visitors]
    assert_equal 1, metrics[:visits]
  end

  test "referrer Direct filter matches visits without referrer" do
    @visit_one.update!(referring_domain: nil, referrer: nil)
    @visit_two.update!(referring_domain: "example.com")
    compute_all

    overview = Analytics::Overview.new(filter_params: {
      start_on: @start_on,
      end_on: @end_on,
      referrer: "Direct"
    })
    metrics = overview.metrics

    assert_equal 1, metrics[:visitors]
    assert_equal 1, metrics[:visits]
  end

  # --- Channel classification ---

  test "channel breakdown classifies search referrer" do
    @visit_one.update!(referring_domain: "google.com")
    @visit_two.update!(referring_domain: "bing.com")
    compute_all

    overview = Analytics::Overview.new(filter_params: { start_on: @start_on, end_on: @end_on })
    labels = overview.breakdowns[:channel].map { |row| row[:label] }

    assert_includes labels, "search"
  end

  test "channel breakdown classifies social referrer" do
    @visit_one.update!(referring_domain: "facebook.com")
    @visit_two.update!(referring_domain: nil, referrer: nil)
    compute_all

    overview = Analytics::Overview.new(filter_params: { start_on: @start_on, end_on: @end_on })
    labels = overview.breakdowns[:channel].map { |row| row[:label] }

    assert_includes labels, "social"
    assert_includes labels, "direct"
  end

  test "channel breakdown classifies direct and email referrers" do
    @visit_one.update!(referring_domain: nil, referrer: nil)
    @visit_two.update!(referring_domain: "mail.google.com", referrer: "https://mail.google.com/")
    compute_all

    overview = Analytics::Overview.new(filter_params: { start_on: @start_on, end_on: @end_on })
    labels = overview.breakdowns[:channel].map { |row| row[:label] }

    assert_includes labels, "direct"
    assert_includes labels, "email"
  end

  test "channel filter narrows to matching visits" do
    @visit_one.update!(referring_domain: "google.com")
    @visit_two.update!(referring_domain: nil, referrer: nil)
    compute_all

    overview = Analytics::Overview.new(filter_params: {
      start_on: @start_on,
      end_on: @end_on,
      channel: "search"
    })

    assert_equal 1, overview.metrics[:visitors]
    assert_equal 1, overview.metrics[:visits]
  end

  # --- Series ---

  test "series returns time-bucketed data" do
    overview = Analytics::Overview.new(filter_params: { start_on: @start_on, end_on: @end_on })
    series = overview.series

    assert series[:visitors].values.sum >= 2
    assert series[:visits].values.sum >= 2
    assert series[:pageviews].values.sum >= 3
  end

  # A sparse series drew the chart as a straight line across the range, hiding every quiet day.
  test "series covers every bucket in range, not only the ones with visits" do
    overview = Analytics::Overview.new(filter_params: { start_on: Date.parse("2026-01-08"),
                                                        end_on: Date.parse("2026-01-12") })
    visits = overview.series[:visits]

    assert_equal %w[2026-01-08 2026-01-09 2026-01-10 2026-01-11 2026-01-12], visits.keys
    assert_equal 1, visits["2026-01-10"]
    assert_equal 0, visits["2026-01-11"]
  end

  test "a range with no visit at all charts nothing" do
    overview = Analytics::Overview.new(filter_params: { start_on: Date.parse("2025-06-01"),
                                                        end_on: Date.parse("2025-06-30") })

    assert_not overview.chart.any?
  end

  test "hourly granularity for ranges under 2 days" do
    short_start = Date.parse("2026-01-10")
    short_end = Date.parse("2026-01-10")

    overview = Analytics::Overview.new(filter_params: { start_on: short_start, end_on: short_end })
    series = overview.series

    series[:visits].each_key do |time|
      assert time.to_s.include?(":00:00"), "Expected hour-aligned bucket, got #{time}"
    end
  end

  # --- Breakdowns ---

  test "path breakdown groups pageviews by normalized path" do
    overview = Analytics::Overview.new(filter_params: { start_on: @start_on, end_on: @end_on })
    rows = overview.breakdowns[:path]
    labels = rows.map { |row| row[:label] }
    pageviews_by_path = rows.index_by { |row| row[:label] }

    assert_includes labels, "/home"
    assert_includes labels, "/about"
    assert_includes labels, "/pricing"
    assert_equal 1, pageviews_by_path.fetch("/home")[:pageviews]
    assert_equal 1, pageviews_by_path.fetch("/about")[:pageviews]
    assert_equal 1, pageviews_by_path.fetch("/pricing")[:pageviews]
  end

  test "path breakdown duration averages by visits containing path" do
    repeated_path_visit = create_visit("visit-repeated-path", "visitor-3",
      started_at: Time.zone.parse("2026-01-22 11:00:00"),
      landing_page: "/home")
    create_pageview(repeated_path_visit, "/home", offset: 1.minute)
    create_pageview(repeated_path_visit, "/pricing", offset: 2.minutes)
    create_pageview(repeated_path_visit, "/home", offset: 3.minutes)
    compute_all

    overview = Analytics::Overview.new(filter_params: { start_on: @start_on, end_on: @end_on })
    home_row = overview.breakdowns[:path].find { |row| row[:label] == "/home" }

    assert_equal 3, home_row[:pageviews]
    assert_equal 2, home_row[:visitors]
    # visit-one has 60s duration; repeated-path visit has 120s duration
    assert_equal 90, home_row[:duration]
  end

  test "referrer breakdown includes referring domain" do
    overview = Analytics::Overview.new(filter_params: { start_on: @start_on, end_on: @end_on })
    labels = overview.breakdowns[:referrer].map { |row| row[:label] }

    assert_includes labels, "google.com"
    assert_includes labels, "Direct"
  end

  test "browser breakdown groups by browser" do
    @visit_one.update!(browser: "Chrome")
    @visit_two.update!(browser: "Firefox")
    compute_all

    overview = Analytics::Overview.new(filter_params: { start_on: @start_on, end_on: @end_on })
    labels = overview.breakdowns[:browser].map { |row| row[:label] }

    assert_includes labels, "Chrome"
    assert_includes labels, "Firefox"
  end

  test "country breakdown groups by country" do
    @visit_one.update!(country: "France", country_code: "fr")
    @visit_two.update!(country: "Germany", country_code: "de")
    compute_all

    overview = Analytics::Overview.new(filter_params: { start_on: @start_on, end_on: @end_on })
    rows = overview.breakdowns[:country]
    labels = rows.map { |row| row[:label] }

    assert_includes labels, "France"
    assert_includes labels, "Germany"
  end

  test "breakdown rows include average duration" do
    @visit_one.update!(browser: "Chrome")
    @visit_two.update!(browser: "Chrome")
    compute_all

    overview = Analytics::Overview.new(filter_params: { start_on: @start_on, end_on: @end_on })
    chrome_row = overview.breakdowns[:browser].find { |row| row[:label] == "Chrome" }

    assert chrome_row.key?(:duration)
  end

  private
    def create_visit(token, visitor_token, **attrs)
      defaults = { started_at: Time.zone.parse("2026-01-15 10:00:00") }
      Ahoy::Visit.create!(visit_token: token, visitor_token: visitor_token, **defaults.merge(attrs))
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
