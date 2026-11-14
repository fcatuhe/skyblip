require "test_helper"

class Admin::EventsControllerTest < ActionDispatch::IntegrationTest
  test "forbids a user who is not an admin" do
    sign_in_as users(:amelie)

    get admin_events_path

    assert_response :forbidden
  end

  test "lists the catalog with what each event says, and how often it happened" do
    sign_in_as users(:francois)
    Event.create! name: "device.connected", context: { device: "go-00012a" }, occurred_at: 1.hour.ago

    get admin_events_path

    assert_response :success
    assert_select "th a", text: "device.connected"
    assert_select "tr:not(.admin-unseen) th a", text: "device.connected"
  end

  # A pilot writes in with the code from the app's settings, and the admin reads that browser's story.
  test "a support code narrows the events to that browser's, oldest first" do
    sign_in_as users(:francois)
    Event.create! name: "device.noticed", payload: { key: "link_lost" }, context: { browser: "01AZ018Z", device: "go-00012a" }, occurred_at: 2.minutes.ago
    Event.create! name: "device.connected", context: { browser: "01AZ018Z", device: "go-00012a" }, occurred_at: 1.minute.ago
    Event.create! name: "device.connected", context: { browser: "XXXXXXXX" }, occurred_at: 1.minute.ago

    get admin_events_path(browser: "01az-018z")

    assert_select ".admin-events tbody tr", count: 2
    assert_select ".admin-events tbody tr:first-child th", text: "device.noticed"
    assert_select ".admin-events-facts", text: /key=link_lost/
  end
end
