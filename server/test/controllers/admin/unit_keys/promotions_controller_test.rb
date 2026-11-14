require "test_helper"

class Admin::UnitKeys::PromotionsControllerTest < ActionDispatch::IntegrationTest
  test "forbids a user who is not an admin" do
    sign_in_as users(:amelie)

    post admin_unit_key_promotion_path(unit_keys(:contested_pending))

    assert_response :forbidden
    assert unit_keys(:contested_pending).reload.pending?
  end

  test "makes a pending key the unit's own and revokes the one it had" do
    sign_in_as users(:francois)

    post admin_unit_key_promotion_path(unit_keys(:contested_pending))

    assert_redirected_to admin_unit_path(units(:contested))
    assert unit_keys(:contested_pending).reload.active?
    assert unit_keys(:contested_active).reload.revoked?
  end

  test "promotes nothing but a pending key" do
    sign_in_as users(:francois)

    post admin_unit_key_promotion_path(unit_keys(:bound_active))
    unit_keys(:bound_active).revoked!
    post admin_unit_key_promotion_path(unit_keys(:bound_active))

    assert_redirected_to admin_unit_path(units(:bound))
    assert unit_keys(:bound_active).reload.revoked?
  end

  test "a promotion is an event, by the admin who made it" do
    sign_in_as users(:francois)

    assert_event_reported "unit_key.promoted", payload: { unit_id: units(:contested).id, device: "go-000002", by_id: users(:francois).id } do
      post admin_unit_key_promotion_path(unit_keys(:contested_pending))
    end
  end
end
