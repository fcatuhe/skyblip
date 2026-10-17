require "test_helper"

class SessionsControllerTest < ActionDispatch::IntegrationTest
  SAFARI = "Mozilla/5.0 (Macintosh; Intel Mac OS X 14_0) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15"

  test "the sign-in page asks for an email and a password" do
    get new_session_path

    assert_response :success
    assert_select "form[action=?] input[type=email][name=email]", session_path
    assert_select "form[action=?] input[type=password][name=password]", session_path
  end

  test "signs in with the right password" do
    post session_path, params: { email: "Francois@skyblip.test", password: "secret-password" }

    assert_redirected_to root_url
    assert cookies[:session_id]
  end

  # Every visit of that browser then counts as the admin's, before and after signing in.
  test "signing in ties the browser's visit to the user and recomputes its visits" do
    visit = Ahoy::Visit.create!(visit_token: "visit-token", visitor_token: "visitor-token", started_at: 1.minute.ago)
    cookies[:ahoy_visit] = "visit-token"
    cookies[:ahoy_visitor] = "visitor-token"

    assert_enqueued_with job: Analytics::Visit::BackfillVisitorSegmentJob, args: [ "visitor-token" ] do
      post session_path, params: { email: "francois@skyblip.test", password: "secret-password" }, headers: { "User-Agent" => SAFARI }
    end
    assert_equal users(:francois), visit.reload.user
    assert_equal users(:francois).id, visit.events.find_by!(name: "sign_in").user_id
  end

  test "refuses the wrong password" do
    post session_path, params: { email: "francois@skyblip.test", password: "wrong" }

    assert_redirected_to new_session_path
    assert_nil cookies[:session_id]
  end

  test "signs out" do
    sign_in_as users(:francois)

    delete session_path

    assert_redirected_to root_path
    assert_empty cookies[:session_id]
  end
end
