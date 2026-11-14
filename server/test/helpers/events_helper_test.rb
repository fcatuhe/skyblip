require "test_helper"

class EventsHelperTest < ActionView::TestCase
  test "an insight names what the review computes in words, and keeps a value it counted as it is" do
    html = insight_value(without_bluetooth: 1, without_bluetooth_on: { "Safari on iOS" => 1 })

    assert_includes html, "<dt>Without bluetooth</dt>"
    assert_includes html, "<dt>Safari on iOS</dt>"
  end
end
