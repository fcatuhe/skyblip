require "test_helper"

class EventTest < ActiveSupport::TestCase
  test "an event keeps its payload, who and where it came from, and masks what the filter names" do
    Current.session = users(:francois).sessions.create!
    Rails.event.set_context request_id: "r-1", device: "go-00012a"
    Rails.event.notify "unit_key.revoked", unit_id: 7, by_id: 1, secret: "kept out"

    event = Event.last
    assert_equal "unit_key.revoked", event.name
    assert_equal({ "unit_id" => 7, "by_id" => 1, "secret" => "[FILTERED]" }, event.payload)
    assert_equal [ users(:francois).id, "r-1", "go-00012a" ], [ event.user_id, event.request_id, event.reload.device ]
    assert_match %r{\Atest/models/event_test.rb:\d+\z}, event.source
  end

  test "a framework trace is kept without its messages, and the framework's chatter is not kept at all" do
    Rails.event.notify "active_job.completed", job_class: "Analytics::Visit::ComputeJob", duration: 12.5,
      exception_class: "ActiveRecord::RecordNotFound", exception_message: "Couldn't find Visit with 'id'=3", arguments: [ 1 ]
    Rails.event.notify "active_record.sql", sql: "SELECT 1"

    assert_equal [ "active_job.completed" ], Event.pluck(:name)
    assert_equal({ "job_class" => "Analytics::Visit::ComputeJob", "duration" => 12.5, "exception_class" => "ActiveRecord::RecordNotFound" }, Event.last.payload)
  end

  # An event says what became true, so one raised in a transaction that rolls back never happened.
  test "an event is written once its transaction commits, and never when it rolls back" do
    Unit.transaction do
      Rails.event.notify "unit.enrolled", unit_id: 1
      raise ActiveRecord::Rollback
    end
    Unit.transaction { Rails.event.notify "unit.enrolled", unit_id: 2 }

    assert_equal [ 2 ], Event.where(name: "unit.enrolled").map { it.payload["unit_id"] }
  end

  # Every open briefing posts its journal every few seconds, so a busy endpoint keeps only the posts it refused.
  test "a busy endpoint's request is kept only when it was refused" do
    Rails.event.notify "action_controller.request_completed", controller: "EventsController", action: "create", status: 204
    Rails.event.notify "action_controller.request_completed", controller: "EventsController", action: "create", status: 429
    Rails.event.notify "action_controller.request_completed", controller: "Briefing::DevicesController", action: "show", status: 200

    assert_equal [ 429, 200 ], Event.where(name: "action_controller.request_completed").order(:id).map { it.payload["status"] }
  end

  test "pruning keeps traces and clicks for 13 months and events for 25" do
    kept = [
      Event.create!(name: "action_controller.request_completed", occurred_at: 13.months.ago + 1.day),
      Event.create!(name: "browser.clicked", occurred_at: 13.months.ago + 1.day),
      Event.create!(name: "device.noticed", occurred_at: 25.months.ago + 1.day)
    ]
    Event.create!(name: "action_controller.request_completed", occurred_at: 13.months.ago - 1.day)
    Event.create!(name: "browser.clicked", occurred_at: 13.months.ago - 1.day)
    Event.create!(name: "device.noticed", occurred_at: 25.months.ago - 1.day)

    Event.prune

    assert_equal kept, Event.order(:id).to_a
  end

  test "an admin's read of the events is narrowed by device, browser and name, and logged" do
    Event.create!(name: "device.noticed", context: { device: "go-00012a", browser: "01AZ018Z" }, occurred_at: 2.minutes.ago)
    Event.create!(name: "device.connected", context: { device: "go-00012a", browser: "01AZ018Z" }, occurred_at: 1.minute.ago)
    Event.create!(name: "device.noticed", context: { device: "bravo-3f2a", browser: "XXXXXXXX" }, occurred_at: 1.minute.ago)

    listed = Event.list(by: users(:francois), device: "go-00012a", name: "device.", last: 10)

    assert_equal %w[device.noticed device.connected], listed.map(&:name)
    assert_equal [ "01AZ018Z" ], Event.tail(name: "device.noticed", browser: "01AZ018Z").map(&:browser)
    logged = Event.find_by!(name: "event.listed")
    assert_equal({ "by_id" => users(:francois).id, "device" => "go-00012a", "prefix" => "device.", "limit" => 10 }, logged.payload)
  end
end
