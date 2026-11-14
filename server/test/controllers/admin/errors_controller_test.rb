require "test_helper"

class Admin::ErrorsControllerTest < ActionDispatch::IntegrationTest
  setup { sign_in_as users(:francois) }

  test "lists what broke on the server and in browsers, grouped by problem" do
    2.times { Rails.error.report RuntimeError.new("the challenge expired"), handled: true }
    Event.create! name: "browser.failed", payload: { error: "TypeError", message: "x is undefined", fingerprint: "0123456789abcdef" }, occurred_at: 1.minute.ago

    get admin_errors_path

    assert_response :success
    assert_select "tbody tr", count: 2
    assert_select "tbody th a", text: "RuntimeError"
    assert_select "tbody th a", text: "TypeError"
  end

  test "a problem shows where it was raised and its latest occurrences" do
    Rails.error.report RuntimeError.new("the challenge expired"), handled: true
    problem = Event::Problem.since(1.day.ago).sole

    get admin_error_path(problem.fingerprint)

    assert_select "h1", text: "RuntimeError"
    assert_select ".admin-frames"
    assert_select ".admin-events tbody tr", count: 1
  end

  test "an unknown problem is not found" do
    get admin_error_path("ffffffffffffffff")

    assert_response :not_found
  end
end
