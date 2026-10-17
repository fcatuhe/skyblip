require "test_helper"

class UserTest < ActiveSupport::TestCase
  test "normalizes the email and names the user in full" do
    user = User.new(first_name: "Jean", last_name: "Mermoz", email: " Jean@Aeropostale.TEST ")

    assert_equal "jean@aeropostale.test", user.email
    assert_equal "Jean Mermoz", user.name
  end

  test "is a user unless made an admin" do
    user = User.create!(first_name: "Jean", last_name: "Mermoz", email: "jean@aeropostale.test", password: "a-long-password")

    assert user.user?
    assert_not user.admin?
  end

  test "refuses a short password, and keeps the old one when left blank" do
    assert_not users(:amelie).update(password: "too-short")

    user = User.find(users(:amelie).id)
    assert user.update(password: "", password_confirmation: "")
    assert user.reload.authenticate("secret-password")
  end

  test "an admin administers others but not herself" do
    assert users(:francois).can_administer?(users(:amelie))
    assert_not users(:francois).can_administer?(users(:francois))
    assert_not users(:amelie).can_administer?(users(:francois))
  end
end
