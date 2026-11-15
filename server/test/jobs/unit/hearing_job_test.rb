require "test_helper"

class Unit::HearingJobTest < ActiveJob::TestCase
  test "hears the journal entries it is handed, by their uuid" do
    read = Event.create!(uuid: SecureRandom.uuid, name: "device.read", occurred_at: 1.hour.ago, payload: {
      device_id: "0c0c0c0c0c0c0c0c", product: "bravo", variant: "standard", version: "0.2.0+950", release: true, bluetooth_tag: 0x0c0c })

    Unit::HearingJob.perform_now([ read.uuid ], "203.0.113.9")

    assert_equal [ "bravo-0c0c", [ "0.2.0+950" ] ], [ Unit.find_by!(device_id: "0c0c0c0c0c0c0c0c").slug, Unit.last.firmwares.pluck(:version) ]
  end
end
