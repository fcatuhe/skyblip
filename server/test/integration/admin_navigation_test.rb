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

    assert_select ".site-nav a[href=?]", admin_analytics_path(filters: { segment: "non_user" }), text: "Admin"
  end

  test "a user who is not an admin does not" do
    sign_in_as users(:amelie)

    get root_path

    assert_select "a", text: "Admin", count: 0
  end

  test "the admin area speaks English, from a French page too" do
    sign_in_as users(:francois)

    get root_path(locale: :fr)
    assert_select ".site-nav a[href=?]", "/admin/analytics?filters%5Bsegment%5D=non_user", text: "Admin"

    get admin_analytics_path(locale: :fr)
    assert_select "h1", text: "Analytics"
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

  # A window opened with noopener leaves the app's browsing context group, so the next link would open yet another window.
  test "the jobs dashboard opens in its own window, which every link reuses" do
    sign_in_as users(:francois)

    get admin_users_path

    assert_select "a[href^=?]", admin_mission_control_jobs_path, minimum: 1
    assert_select "a[href^=?]:not([target=mission_control])", admin_mission_control_jobs_path, count: 0
    assert_select "a[href^=?][rel~=noopener]", admin_mission_control_jobs_path, count: 0
  end

  test "the jobs dashboard leaves the site its French, even as the first page to translate" do
    sign_in_as users(:francois)

    I18n.reload!
    get admin_mission_control_jobs_path
    assert_response :success

    reset!
    get briefing_root_path, headers: { "Accept-Language" => "fr-FR" }
    assert_select "html[lang=fr]"
    assert_select "h1", text: "Votre skyBlip"
  end
end
