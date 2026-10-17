require "test_helper"

class SessionsControllerTest < ActionDispatch::IntegrationTest
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
