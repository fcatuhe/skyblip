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
end
