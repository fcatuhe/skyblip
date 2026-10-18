require "test_helper"

class Admin::UsersControllerTest < ActionDispatch::IntegrationTest
  test "sends a visitor to the sign-in page" do
    get admin_users_path

    assert_redirected_to new_session_path
  end

  test "forbids a user who is not an admin" do
    sign_in_as users(:amelie)

    get admin_users_path

    assert_response :forbidden
  end

  test "lists the users" do
    sign_in_as users(:francois)

    get admin_users_path

    assert_response :success
    assert_select "a[href=?]", admin_user_path(users(:amelie)), text: "Amélie Pilot"
  end

  test "creates a user with a name, an email, a password and a role" do
    sign_in_as users(:francois)

    assert_difference -> { User.count } do
      post admin_users_path, params: { user: { first_name: "Jean", last_name: "Mermoz", email: "jean@aeropostale.test",
                                               role: "admin", password: "a-long-password", password_confirmation: "a-long-password" } }
    end

    user = User.find_by!(email: "jean@aeropostale.test")
    assert_redirected_to admin_user_path(user)
    assert user.admin?
    assert user.authenticate("a-long-password")
  end

  test "renders the form again on a short password" do
    sign_in_as users(:francois)

    post admin_users_path, params: { user: { first_name: "Jean", last_name: "Mermoz", email: "jean@aeropostale.test",
                                             role: "user", password: "short", password_confirmation: "short" } }

    assert_response :unprocessable_content
    assert_select "[role=alert] li", text: /Password is too short/
  end

  test "updates a user and keeps the password left blank" do
    sign_in_as users(:francois)

    patch admin_user_path(users(:amelie)), params: { user: { role: "admin", password: "", password_confirmation: "" } }

    assert_redirected_to admin_user_path(users(:amelie))
    assert users(:amelie).reload.admin?
    assert users(:amelie).authenticate("secret-password")
  end

  # A password changed because a session leaked must end that session.
  test "a new password signs the user out everywhere but here" do
    users(:amelie).sessions.create!
    elsewhere = users(:francois).sessions.create!
    sign_in_as users(:francois)
    here = Current.session

    patch admin_user_path(users(:amelie)), params: { user: { password: "a-new-long-password", password_confirmation: "a-new-long-password" } }
    patch admin_user_path(users(:francois)), params: { user: { password: "a-new-long-password", password_confirmation: "a-new-long-password" } }

    assert_empty users(:amelie).sessions.reload
    assert_equal [ here ], users(:francois).sessions.reload.to_a
    assert_not Session.exists?(elsewhere.id)
  end

  test "deletes another user, never the admin signed in" do
    sign_in_as users(:francois)

    assert_difference -> { User.count }, -1 do
      delete admin_user_path(users(:amelie))
    end
    assert_redirected_to admin_users_path

    delete admin_user_path(users(:francois))
    assert_response :forbidden
  end
end
