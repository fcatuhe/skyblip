require "test_helper"

class AdminNavigationTest < ActionDispatch::IntegrationTest
  test "the site links to no sign-in page" do
    get root_path

    assert_response :success
    assert_select "a[href^='#{new_session_path}']", count: 0
    assert_select "a", text: "Admin", count: 0
  end

  test "an admin finds the admin area in the site nav" do
    sign_in_as users(:francois)

    get root_path

    assert_select ".site-nav a[href=?]", admin_users_path, text: "Admin"
  end

  test "a user who is not an admin does not" do
    sign_in_as users(:amelie)

    get root_path

    assert_select "a", text: "Admin", count: 0
  end

  test "the admin area speaks English, from a French page too" do
    sign_in_as users(:francois)

    get root_path(locale: :fr)
    assert_select ".site-nav a[href=?]", "/admin/users", text: "Admin"

    get admin_users_path(locale: :fr)
    assert_select "h1", text: "Users"
  end

  test "the admin area links its sections and signs out" do
    sign_in_as users(:francois)

    get admin_users_path

    assert_select ".admin-nav a[href=?]", admin_users_path, text: "Users"
    assert_select ".admin-nav a[href=?]", admin_mission_control_jobs_path, text: "Jobs"
    assert_select ".admin-nav form[action=?] button", session_path, text: "Sign out"
  end

  test "the jobs dashboard is for admins only" do
    get admin_mission_control_jobs_path
    assert_redirected_to "http://www.example.com/session/new"

    sign_in_as users(:amelie)
    get admin_mission_control_jobs_path
    assert_response :forbidden

    sign_in_as users(:francois)
    get admin_mission_control_jobs_path
    assert_response :success
  end
end
