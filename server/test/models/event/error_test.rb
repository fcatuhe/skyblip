require "test_helper"

class Event::ErrorTest < ActiveSupport::TestCase
  test "a resolved error stays resolved until it happens again, then it is reopened" do
    expire_challenge
    error = Event::Error.since(1.day.ago).sole

    assert_event_reported "error.resolved", payload: { fingerprint: error.fingerprint, by_id: users(:francois).id } do
      error.resolve(by: users(:francois))
    end
    assert Event::Error.since(1.day.ago).sole.resolved?

    travel 1.minute do
      expire_challenge
      error = Event::Error.since(1.day.ago).sole

      assert_not error.resolved?
      assert error.reopened?
      assert_equal true, error.to_h[:reopened]
    end
  end

  test "resolving a reopened error again moves its resolution to now" do
    expire_challenge
    Event::Error.since(1.day.ago).sole.resolve(by: users(:francois))

    travel 1.minute do
      expire_challenge
      Event::Error.since(1.day.ago).sole.resolve(by: users(:francois))

      error = Event::Error.since(1.day.ago).sole
      assert error.resolved?
      assert_equal Time.current.to_i, error.resolution.resolved_at.to_i
    end
  end

  test "an error reopened by hand loses its resolution" do
    expire_challenge
    error = Event::Error.since(1.day.ago).sole
    error.resolve(by: users(:francois))

    assert_event_reported "error.reopened", payload: { fingerprint: error.fingerprint, by_id: users(:francois).id } do
      error.reopen(by: users(:francois))
    end
    assert_nil Event::Error.find_by(fingerprint: error.fingerprint).resolution
  end

  # A browser flushes its journal late, so an older occurrence can arrive after a newer one.
  test "an error that happened again after its resolution stays reopened when an older occurrence arrives late" do
    fail_in_browser at: 3.hours.ago
    Event::Error.since(1.day.ago).sole.resolve(by: users(:francois))
    recurrence = fail_in_browser at: 1.minute.from_now
    fail_in_browser at: 2.hours.ago

    error = Event::Error.since(1.day.ago).sole
    assert error.reopened?
    assert_equal recurrence, error.latest
    assert_equal recurrence, Event::Error.find_by(fingerprint: error.fingerprint).latest
  end

  test "the latest occurrence of each error is the one that happened last, and the newest row breaks a tie" do
    at = 1.hour.ago
    fail_in_browser at:, fingerprint: "a"
    tied = fail_in_browser at:, fingerprint: "a"
    later = fail_in_browser at: 1.minute.ago, fingerprint: "b"
    fail_in_browser at: 2.hours.ago, fingerprint: "b"

    assert_equal({ "a" => tied, "b" => later }, Event::Error.since(1.day.ago).to_h { [ it.fingerprint, it.latest ] })
  end

  private
    def fail_in_browser(at:, fingerprint: "0123456789abcdef")
      Event.create! name: "browser.failed", payload: { error: "TypeError", message: "x is undefined", fingerprint: }, occurred_at: at
    end

    def expire_challenge = Rails.error.report(RuntimeError.new("the challenge expired"), handled: true)
end
