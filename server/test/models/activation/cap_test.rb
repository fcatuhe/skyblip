require "test_helper"

class Activation::CapTest < ActiveSupport::TestCase
  test "starts at 50 new units a day, in one row" do
    assert_equal 50, Activation::Cap.current.new_units_per_day
    assert_equal Activation::Cap.current, Activation::Cap.current
    assert_equal 1, Activation::Cap.count
  end

  test "takes a whole number of units above zero, and the database refuses zero on its own" do
    cap = Activation::Cap.current

    [ 0, -1, 2.5, nil ].each { assert_not cap.update(new_units_per_day: it), it.inspect }
    assert_raises(ActiveRecord::StatementInvalid) { cap.update_column(:new_units_per_day, 0) }
  end

  test "a change is an event with the cap before, after, and the admin who made it" do
    Current.session = users(:francois).sessions.create!

    assert_event_reported "activation_cap.changed", payload: { from: 50, to: 80, by_id: users(:francois).id } do
      Activation::Cap.current.update!(new_units_per_day: 80)
    end
  end
end
