require "test_helper"

class Analytics::Visit::DurationTest < ActiveSupport::TestCase
  test "zero with fewer than two events" do
    assert_equal 0, duration_for([]).in_seconds
    assert_equal 0, duration_for([ event("$view", 0) ]).in_seconds
  end

  test "simple duration from first to last event" do
    events = [
      event("$view", 0),
      event("$view", 120)
    ]

    assert_equal 120, duration_for(events).in_seconds
  end

  test "counts only visible time with visibility events" do
    events = [
      event("$view", 0),
      event("$visibilitychange", 60, state: "hidden"),
      event("$visibilitychange", 300, state: "visible"),
      event("$view", 360)
    ]

    # visible 0→60 (60s) + visible 300→360 (60s) = 120s
    assert_equal 120, duration_for(events).in_seconds
  end

  test "pagehide closes visible span" do
    events = [
      event("$view", 0),
      event("$pagehide", 60)
    ]

    assert_equal 60, duration_for(events).in_seconds
  end

  test "multiple hide/show cycles" do
    events = [
      event("$view", 0),
      event("$visibilitychange", 30, state: "hidden"),
      event("$visibilitychange", 100, state: "visible"),
      event("$visibilitychange", 130, state: "hidden"),
      event("$visibilitychange", 200, state: "visible"),
      event("$view", 250)
    ]

    # visible 0→30 (30s) + 100→130 (30s) + 200→250 (50s) = 110s
    assert_equal 110, duration_for(events).in_seconds
  end

  test "hidden at end counts only visible portion" do
    events = [
      event("$view", 0),
      event("$visibilitychange", 45, state: "hidden"),
      event("$pagehide", 300)
    ]

    # visible 0→45 (45s), hidden rest
    assert_equal 45, duration_for(events).in_seconds
  end

  test "pagehide then visibility hidden does not double count" do
    events = [
      event("$view", 0),
      event("$pagehide", 60),
      event("$visibilitychange", 60, state: "hidden")
    ]

    # pagehide closes the span at 60s, hidden is a no-op
    assert_equal 60, duration_for(events).in_seconds
  end

  test "visibility hidden then pagehide does not double count" do
    events = [
      event("$view", 0),
      event("$visibilitychange", 60, state: "hidden"),
      event("$pagehide", 60)
    ]

    # hidden closes the span at 60s, pagehide is a no-op
    assert_equal 60, duration_for(events).in_seconds
  end

  test "reopens span on regular event after hidden" do
    events = [
      event("$view", 0),
      event("$visibilitychange", 30, state: "hidden"),
      event("$view", 100),
      event("$view", 160)
    ]

    # visible 0→30 (30s) + 100→160 (60s) = 90s
    assert_equal 90, duration_for(events).in_seconds
  end

  private

    def duration_for(events)
      Analytics::Visit::Duration.new(events)
    end

    def event(name, offset_seconds, state: nil)
      time = Time.zone.parse("2026-01-15 10:00:00") + offset_seconds.seconds
      properties = state ? { "state" => state } : {}
      Ahoy::Event.new(name:, time:, properties:)
    end
end
