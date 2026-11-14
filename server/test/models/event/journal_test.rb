require "test_helper"

class Event::JournalTest < ActiveSupport::TestCase
  BROWSER = "01AZ018Z"

  test "an entry is kept with the keys its event names, each of its type, and the page it came from" do
    Event::Journal.new(browser: BROWSER, entries: [
      entry("device.noticed", device: "go-00012a", path: "/briefing/devices/go-00012a/firmware?activated=1",
        properties: { key: "link_lost", detail: "GATT Server is disconnected.", phase: "offline", password: "nope", confirmed: "yes" })
    ]).record

    event = Event.sole
    assert_equal({ "key" => "link_lost", "detail" => "GATT Server is disconnected.", "phase" => "offline" }, event.payload)
    assert_equal [ "go-00012a", BROWSER, "/briefing/devices/go-00012a/firmware" ], [ event.device, event.browser, event.context["path"] ]
    assert_equal Time.utc(2026, 11, 14, 10), event.occurred_at
  end

  # A pilot's phone posts its journal again until it is answered, and a beacon may land twice.
  test "an entry that arrives twice is kept once" do
    journal = Event::Journal.new(browser: BROWSER, entries: [ entry("device.connected") ])

    2.times { journal.record }

    assert_equal 1, Event.count
  end

  test "an event the browser may not send, an entry without an id, or a journal from no browser is not kept" do
    Event::Journal.new(browser: BROWSER, entries: [ entry("unit_key.revoked"), entry("nothing.at_all"), entry("device.connected", id: "1") ]).record

    assert_equal 0, Event.count
    assert_not Event::Journal.new(browser: "not a code", entries: [ entry("device.connected") ]).valid?
  end

  test "an entry from a clock far ahead is placed now, and one a month old at the oldest the journal keeps" do
    travel_to Time.utc(2026, 11, 14, 10) do
      Event::Journal.new(browser: BROWSER, entries: [
        entry("device.connected", at: "2027-01-01T00:00:00Z"), entry("device.read", at: "2026-01-01T00:00:00Z"), entry("device.noticed", at: "soon")
      ]).record

      assert_equal [ Time.current, 30.days.ago, Time.current ], Event.order(:id).map(&:occurred_at)
    end
  end

  test "a script failure is fingerprinted by its kind, class, message and file, whatever its numbers and asset digest" do
    failure = ->(message, file) { entry("browser.failed", properties: { kind: "error", error: "TypeError", message:, file: }) }
    Event::Journal.new(browser: BROWSER, entries: [
      failure.("Cannot read 'x' of undefined at 12", "/assets/skyblip/update-1a2b3c4d.js"),
      failure.("Cannot read 'y' of undefined at 47", "/assets/skyblip/update-9f8e7d6c.js"),
      failure.("Cannot read 'x' of undefined at 12", "/assets/skyblip/fleet-1a2b3c4d.js")
    ]).record

    first, again, other = Event.order(:id).map(&:fingerprint)
    assert_equal first, again
    assert_not_equal first, other
  end

  private
    def entry(name, id: SecureRandom.uuid, at: "2026-11-14T10:00:00Z", **fields)
      { id:, name:, at:, seq: 1, page: SecureRandom.uuid, **fields }
    end
end
