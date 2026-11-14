require "test_helper"

class Admin::ActivationCapsControllerTest < ActionDispatch::IntegrationTest
  test "forbids a user who is not an admin" do
    sign_in_as users(:amelie)

    patch admin_activation_cap_path, params: { activation_cap: { new_units_per_day: 500 } }

    assert_response :forbidden
    assert_equal 50, Unit.daily_cap
  end

  test "an admin changes the daily cap from the units page" do
    sign_in_as users(:francois)

    get admin_units_path
    assert_select "dd", text: /50 new units a day/ do
      assert_select "a[href=?]", edit_admin_activation_cap_path
    end

    patch admin_activation_cap_path, params: { activation_cap: { new_units_per_day: 120 } }
    assert_redirected_to admin_units_path
    assert_equal 120, Unit.daily_cap
  end

  test "a cap that is not a whole number above zero is said, and kept as it was" do
    sign_in_as users(:francois)

    patch admin_activation_cap_path, params: { activation_cap: { new_units_per_day: 0 } }

    assert_response :unprocessable_content
    assert_select "[role=alert]", text: /greater than 0/
    assert_equal 50, Unit.daily_cap
  end
end
