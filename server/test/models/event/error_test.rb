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

  private
    def expire_challenge = Rails.error.report(RuntimeError.new("the challenge expired"), handled: true)
end
