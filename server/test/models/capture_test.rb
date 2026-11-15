require "test_helper"

class CaptureTest < ActiveSupport::TestCase
  test "a capture reads its session, records and firmware off its own lines" do
    capture = Capture.create!(device: "go-00012a", browser: "01AZ018Z", profile: "power_run", body: capture_body(records: 960))

    assert_equal [ 1785628800, 960, "0.4.1+1702" ], [ capture.session, capture.records, capture.firmware ]
    assert_equal capture_body(records: 960).bytesize, capture.bytes
    assert_equal "go-00012a-1785628800.ndjson.gz", capture.filename
  end

  # A full capture holds positions and the aircraft heard, so it is kept the shorter time.
  test "a full capture is kept 13 months and a power or flight run 25" do
    freeze_time do
      full = Capture.create!(device: "go-00012a", profile: "full", body: capture_body(profile: "full"))
      run = Capture.create!(device: "bravo-3f2a", profile: "flight_run", body: capture_body(profile: "flight_run"))

      assert_equal 13.months.from_now, full.kept_until
      assert_equal 25.months.from_now, run.kept_until
    end
  end

  test "a body that is not a gzipped capture session, or names another profile, is refused" do
    assert_not Capture.new(device: "go-00012a", profile: "full", body: "not gzip").valid?
    assert_not Capture.new(device: "go-00012a", profile: "full", body: capture_body(profile: "full", note_type: "flight")).valid?
    assert_not Capture.new(device: "go-00012a", profile: "full", body: capture_body(profile: "power_run")).valid?
    assert_not Capture.new(device: "activate-3f2a", profile: "full", body: capture_body(profile: "full")).valid?
  end

  test "a body past the ceiling is refused" do
    capture = Capture.new(device: "go-00012a", profile: "full", body: capture_body(profile: "full") + "x" * Capture::MAX_BYTES)
    assert_not capture.valid?
    assert capture.errors.of_kind?(:bytes, :less_than_or_equal_to)
  end

  test "pruning deletes the captures past their keeping, and only those" do
    kept = Capture.create!(device: "go-00012a", profile: "full", body: capture_body(profile: "full"))
    expired = Capture.create!(device: "go-00012a", profile: "full", body: capture_body(profile: "full"))
    expired.update_column :kept_until, 1.minute.ago

    Capture.prune

    assert Capture.exists?(kept.id)
    assert_not Capture.exists?(expired.id)
  end

  test "a capture received is an event with its device and what it holds" do
    capture = Capture.create!(device: "bravo-3f2a", profile: "power_run", body: capture_body)

    event = Event.find_by!(name: "capture.received")
    assert_equal "bravo-3f2a", event.device
    assert_equal({ "capture_id" => capture.id, "profile" => "power_run", "session" => 1785628800, "records" => 2,
                   "bytes" => capture.bytes, "firmware" => "0.4.1+1702", "device" => "bravo-3f2a" }, event.payload)
  end
end
