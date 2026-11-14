require "test_helper"

class EventsControllerTest < ActionDispatch::IntegrationTest
  test "a browser's journal is kept, with the admin who sent it" do
    sign_in_as users(:francois)

    post events_path, params: { browser: "01AZ018Z", entries: [
      { id: SecureRandom.uuid, name: "device.read", at: Time.current.iso8601, seq: 1, page: SecureRandom.uuid, device: "go-00012a",
        properties: { running: "0.2.0+15", activated: true, battery_percent: 80 } }
    ] }, as: :json

    assert_response :no_content
    event = Event.find_by!(name: "device.read")
    assert_equal({ "running" => "0.2.0+15", "activated" => true, "battery_percent" => 80 }, event.payload)
    assert_equal [ users(:francois).id, "go-00012a" ], [ event.user_id, event.device ]
  end

  test "a journal from no browser is refused, and so is a post with no entries" do
    post events_path, params: { browser: "", entries: [ { id: SecureRandom.uuid, name: "device.read" } ] }, as: :json
    assert_response :unprocessable_content

    post events_path, params: { browser: "01AZ018Z" }, as: :json
    assert_response :bad_request
  end
end
