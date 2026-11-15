require "test_helper"

class Admin::Errors::ResolutionsControllerTest < ActionDispatch::IntegrationTest
  setup do
    sign_in_as users(:francois)
    Rails.error.report RuntimeError.new("the challenge expired"), handled: true
    @error = Event::Error.since(1.day.ago).sole
  end

  test "an admin resolves an error, which leaves the list for the resolved ones" do
    post admin_error_resolution_path(@error.fingerprint)

    assert_redirected_to admin_error_path(@error.fingerprint)
    follow_redirect!
    assert_select "button", text: "Reopen"

    get admin_errors_path
    assert_select "p", text: "Nothing broke in the last 7 days that is not resolved."
    assert_select "h2", text: "Resolved"
  end

  test "an admin reopens a resolved error" do
    @error.resolve(by: users(:francois))

    delete admin_error_resolution_path(@error.fingerprint)

    follow_redirect!
    assert_select "button", text: "Resolve"
    assert_select "dt", text: "Resolved", count: 0
  end

  test "an unknown error cannot be resolved" do
    post admin_error_resolution_path("ffffffffffffffff")

    assert_response :not_found
  end
end
