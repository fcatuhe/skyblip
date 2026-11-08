require "test_helper"

class Admin::UnitsControllerTest < ActionDispatch::IntegrationTest
  test "forbids a user who is not an admin" do
    sign_in_as users(:amelie)

    get admin_units_path

    assert_response :forbidden
  end

  test "lists the units by address and build, and the server key this server holds" do
    sign_in_as users(:francois)

    get admin_units_path

    assert_response :success
    assert_select "a[href=?]", admin_unit_path(units(:contested)), text: "000002"
    assert_select "tr", text: /000001.*release/m
    assert_select "dd", text: /890026b1, public key #{vectors["server_public_key"]}/
  end

  test "shows a unit's keys with what an admin may do to each" do
    sign_in_as users(:francois)

    get admin_unit_path(units(:contested))

    assert_response :success
    assert_select "form[action=?]", admin_unit_key_promotion_path(unit_keys(:contested_pending))
    assert_select "form[action=?]", admin_unit_key_revocation_path(unit_keys(:contested_active))
    assert_select "tr", text: /#{unit_keys(:contested_pending).public_key}.*development/m
  end

  test "flags the units that share a key, on the list and beside the key" do
    sign_in_as users(:francois)
    clone = Unit.create!(device_id: vectors["device_id"], serial: 3)
    clone.keys.create!(public_key: unit_keys(:bound_active).public_key, status: :active)

    get admin_units_path
    assert_select "[role=alert]", text: /share a key/ do
      assert_select "a[href=?]", admin_unit_path(units(:bound)), text: "000001"
      assert_select "a[href=?]", admin_unit_path(clone), text: "000003"
      assert_select "a[href=?]", admin_unit_path(units(:contested)), count: 0
    end

    get admin_unit_path(units(:bound))
    assert_select "tr", text: /#{unit_keys(:bound_active).public_key}.*000003/m
  end

  test "flags nothing while every key is on one unit" do
    sign_in_as users(:francois)

    get admin_units_path

    assert_select "[role=alert]", text: /share a key/, count: 0
  end
end
