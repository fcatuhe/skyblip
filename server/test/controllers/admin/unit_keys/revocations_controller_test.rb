require "test_helper"

class Admin::UnitKeys::RevocationsControllerTest < ActionDispatch::IntegrationTest
  test "forbids a user who is not an admin" do
    sign_in_as users(:amelie)

    post admin_unit_key_revocation_path(unit_keys(:bound_active))

    assert_response :forbidden
    assert unit_keys(:bound_active).reload.active?
  end

  test "revokes the active key, so the next registration binds as a first one" do
    sign_in_as users(:francois)

    post admin_unit_key_revocation_path(unit_keys(:bound_active))

    assert_redirected_to admin_unit_path(units(:bound))
    assert unit_keys(:bound_active).reload.revoked?
    assert_nil units(:bound).reload.active_key
  end

  test "revokes nothing but the active key" do
    sign_in_as users(:francois)

    post admin_unit_key_revocation_path(unit_keys(:contested_pending))

    assert_redirected_to admin_unit_path(units(:contested))
    assert unit_keys(:contested_pending).reload.pending?
  end
end
