require "test_helper"

class Admin::CapturesControllerTest < ActionDispatch::IntegrationTest
  setup { sign_in_as users(:francois) }

  test "lists the captures pilots sent, newest first" do
    Capture.create!(device: "go-00012a", profile: "power_run", body: capture_body)
    Capture.create!(device: "bravo-3f2a", profile: "full", body: capture_body(profile: "full"))

    get admin_captures_path

    assert_response :success
    assert_select "tbody tr", count: 2
    assert_select "tbody tr:first-child th a", text: "bravo-3f2a"
  end

  # A full capture holds positions, so each download is on the record with who made it.
  test "a download sends the gzipped capture, and says who read it" do
    capture = Capture.create!(device: "go-00012a", profile: "full", body: capture_body(profile: "full"))

    get admin_capture_path(capture)

    assert_response :success
    assert_equal capture.body, response.body
    assert_match "go-00012a-1785628800.ndjson.gz", response.headers["Content-Disposition"]
    assert_equal users(:francois).id, Event.find_by!(name: "capture.downloaded").payload["by_id"]
  end
end
