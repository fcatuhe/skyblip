require "test_helper"

class ScaleTest < ActiveSupport::TestCase
  test "ceiling rounds a peak up to the next round step" do
    assert_equal 15, Scale.ceiling(11)
    assert_equal 25, Scale.ceiling(21)
    assert_equal 400, Scale.ceiling(310)
    assert_equal 10_000, Scale.ceiling(6_001)
  end

  test "ceiling leaves a peak that already sits on a step" do
    assert_equal 5, Scale.ceiling(5)
    assert_equal 1_000, Scale.ceiling(1_000)
  end

  test "ceiling of nothing is nothing, so an empty chart keeps a flat baseline" do
    assert_equal 0, Scale.ceiling(0)
  end
end
