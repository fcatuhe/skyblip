require "test_helper"

class Event::ErrorReportTest < ActiveSupport::TestCase
  test "an error is kept with its class, message, cause, first frames and the revision that raised it" do
    with_config revision: "4f2a9c1" do
      Rails.error.report raised { raise ArgumentError, "the serial 70000 is out of range" }, handled: true, source: "activation"
    end

    payload = Event.find_by!(name: "error.reported").payload
    assert_equal [ "ArgumentError", "the serial 70000 is out of range", true, "warning", "activation", "4f2a9c1" ],
      payload.values_at("error", "message", "handled", "severity", "source", "revision")
    assert_match %r{\Atest/models/event/error_report_test.rb:\d+:in }, payload["frames"].first
    assert_match(/\A\h{16}\z/, payload["fingerprint"])
  end

  test "an error raised again from the same method is the same problem, whatever its message" do
    first = Event::ErrorReport.new(raised { fail_with "serial 1" }, handled: false, severity: :error, context: {}, source: "test")
    again = Event::ErrorReport.new(raised { fail_with "serial 2" }, handled: false, severity: :error, context: {}, source: "test")
    other = Event::ErrorReport.new(raised { raise "serial 1" }, handled: false, severity: :error, context: {}, source: "test")

    assert_equal first.fingerprint, again.fingerprint
    assert_not_equal first.fingerprint, other.fingerprint
  end

  test "a long message is cut, and the job or controller that raised it is named" do
    report = Event::ErrorReport.new(raised { raise "x" * 400 }, handled: false, severity: :error,
      context: { job: Analytics::Visit::ComputeJob.new }, source: "application.active_job").to_h

    assert_equal 300, report[:message].length
    assert_equal "Analytics::Visit::ComputeJob", report[:job]
  end

  private
    def raised
      yield
    rescue => error
      error
    end

    def fail_with(message) = raise(KeyError, message)
end
