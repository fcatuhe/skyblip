require "test_helper"

class DevicesHelperTest < ActionView::TestCase
  test "the photo widens only on the side a mark writes its word in" do
    assert_equal "0 440 419 493", device_view_box(:go, device_marks(:go, pad: "Pad"))
    assert_equal "-250 440 669 493", device_view_box(:go, device_marks(:go, rst: "RST"))
    assert_equal "-250 440 869 493", device_view_box(:go, device_marks(:go, rst: "RST", usb: "USB-C"))
    assert_equal "0 0 440 356", device_view_box(:bravo, device_marks(:bravo, button: "Press", usb: "Plug in"))
  end

  test "an arrow ends on the contact, its head's base a head's length short of it, and a line to a ring stops outside the ring" do
    rst = device_marks(:go, rst: "RST").first
    assert_equal [ 6.0, 648.0 ], rst.line_end
    assert_equal "24,648 6.0,657.0 6.0,639.0", rst.head

    # The ring around the X1's button has a radius of 38, and the line stops 4 outside it.
    button = device_marks(:bravo, button: "Press").first
    assert_in_delta 42, Math.hypot(button.spot.x - button.line_end[0], button.spot.y - button.line_end[1]), 0.2
  end
end
