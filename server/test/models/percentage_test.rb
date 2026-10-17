require "test_helper"

class PercentageTest < ActiveSupport::TestCase
  test "of returns the share of the whole" do
    assert_equal 25.0, Percentage.of(1, 4)
    assert_equal 66.67, Percentage.of(2, 3)
  end

  test "of returns a float zero when there is nothing to divide" do
    assert_equal 0.0, Percentage.of(0, 0)
    assert_kind_of Float, Percentage.of(0, 0)
  end
end
