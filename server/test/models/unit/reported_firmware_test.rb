require "test_helper"

class Unit::ReportedFirmwareTest < ActiveSupport::TestCase
  test "folds what browsers reported of one unit into stretches on one version, newest first" do
    freeze_time
    slug = units(:bound).slug
    report "device.read", slug, 3.days.ago, running: "0.3.0+1200", track: "release"
    report "device.read", slug, 2.days.ago, running: "0.3.0+1200", track: "release"
    report "device.installed", slug, 1.day.ago, from: "0.3.0+1200", to: "0.4.0+1400"
    report "device.read", slug, 1.hour.ago, running: "0.4.0+1400", track: "development"
    report "device.noticed", slug, 1.hour.ago, key: "link_lost"
    report "device.read", "go-00ffff", 1.hour.ago, running: "9.9.9+1"

    stretches = units(:bound).reported_firmware

    assert_equal [ [ "0.4.0+1400", "development" ], [ "0.3.0+1200", "release" ] ], stretches.map { [ it.version, it.track ] }
    assert_equal [ 1.day.ago, 1.hour.ago ], [ stretches.first.first_seen_at, stretches.first.last_seen_at ]
    assert_equal [ 3.days.ago, 2.days.ago ], [ stretches.last.first_seen_at, stretches.last.last_seen_at ]
  end

  test "a unit no browser reported has no stretch" do
    report "device.read", units(:bound).slug, 1.hour.ago, running: nil

    assert_empty units(:bound).reported_firmware
  end

  private
    def report(name, device, at, **payload)
      Event.create!(name:, payload:, context: { device: }, occurred_at: at)
    end
end
