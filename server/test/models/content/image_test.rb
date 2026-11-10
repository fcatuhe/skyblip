require "test_helper"

class Content::ImageTest < ActiveSupport::TestCase
  test "reads the size of a PNG screen and an AVIF photo from their headers" do
    assert_equal [ 200, 200 ], Content::Image.new("pages/skyblip-go/radar.png").dimensions
    assert_equal [ 19, 25 ], Content::Image.new("pages/skyblip-go/blips/level-steady.png").dimensions
    assert_equal [ 419, 933 ], Content::Image.new("pages/index/skyblip-go-techo.avif").dimensions
  end

  test "refuses a format it cannot read" do
    assert_raises(ArgumentError) { Content::Image.new("share/go.jpg").dimensions }
  end
end
