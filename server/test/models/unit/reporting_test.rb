require "test_helper"

class Unit::ReportingTest < ActiveSupport::TestCase
  DEVICE_ID = "0a0b0c0d0e0f1011"

  setup { freeze_time }

  test "a unit a browser reads for the first time is recorded with no serial, under the identity it stated" do
    hear read(product: "go", variant: "plus", bluetooth_tag: 0x3f2a)

    unit = Unit.find_by!(device_id: DEVICE_ID)
    assert_equal [ nil, "activate-3f2a", "go", "plus" ], [ unit.serial, unit.slug, unit.product, unit.variant ]
    assert_equal [ [ "0.3.0+1234", true, "plus", "read" ] ], unit.firmwares.pluck(:version, :release, :variant, :source)
    assert_equal 2.hours.ago, unit.created_at
  end

  test "a read on the same firmware extends its stretch, and a new version opens another" do
    hear read(at: 3.hours.ago)
    hear read(at: 2.hours.ago)
    hear read(version: "0.4.0+1400", at: 1.hour.ago)

    stretches = Unit.find_by!(device_id: DEVICE_ID).firmwares.newest_first
    assert_equal [ "0.4.0+1400", "0.3.0+1234" ], stretches.pluck(:version)
    assert_equal [ 3.hours.ago, 2.hours.ago ], [ stretches.last.created_at, stretches.last.updated_at ]
  end

  test "a stretch moves from read to installed to attested, and a later read leaves it attested" do
    hear read(version: "0.4.0+1400", at: 2.hours.ago),
      entry("device.installed", device_id: DEVICE_ID, from: "0.3.0+1234", to: "0.4.0+1400", lasted_ms: 41_000)
    unit = Unit.find_by!(device_id: DEVICE_ID)
    assert_equal [ "installed", "0.3.0+1234", 41_000 ], unit.latest_firmware.slice(:source, :from_version, :lasted_ms).values

    unit.attest_firmware(version: "0.4.0+1400", release: true, product: "bravo", variant: "standard")
    hear read(version: "0.4.0+1400")

    assert_equal [ [ "0.4.0+1400", "attested", "standard" ] ], unit.firmwares.pluck(:version, :source, :variant)
  end

  # A registration signs the version and the build only, so the variant a browser read must not pass for signed.
  test "a statement that names no variant opens its own stretch over a read one, and a later read fills no variant in it" do
    hear read(variant: "plus", at: 2.hours.ago)
    unit = Unit.find_by!(device_id: DEVICE_ID)

    unit.attest_firmware(version: "0.3.0+1234", release: true)
    hear read(variant: "plus", at: 1.hour.ago)
    assert_equal [ [ "attested", nil ], [ "read", "plus" ] ], unit.firmwares.newest_first.pluck(:source, :variant)

    unit.attest_firmware(version: "0.3.0+1234", release: true, product: "go", variant: "plus")
    assert_equal [ "attested", "plus" ], unit.latest_firmware.reload.slice(:source, :variant).values
  end

  test "a browser changes neither the product nor the variant an activated unit signed" do
    unit = units(:bound)
    unit.attest_firmware(version: "0.3.0+1300", release: true, product: "go", variant: "plus")

    assert_no_event_reported "unit.variant_changed" do
      hear read(device_id: unit.device_id, product: "bravo", variant: "standard", version: "0.3.0+1300")
    end
    assert_equal [ "go", "plus" ], [ unit.reload.product, unit.variant ]
  end

  test "a journal posted late changes neither the unit's identity nor its firmware history" do
    hear read(variant: "plus", version: "0.4.0+1400", at: 1.hour.ago)
    hear read(variant: "standard", version: "0.3.0+1234", at: 3.hours.ago)

    unit = Unit.find_by!(device_id: DEVICE_ID)
    assert_equal "plus", unit.variant
    assert_equal [ "0.4.0+1400" ], unit.firmwares.pluck(:version)
  end

  test "an install the unit did not come back on marks nothing" do
    hear read, entry("device.installed", device_id: DEVICE_ID, from: "0.3.0+1234", to: "0.4.0+1400")

    assert_equal [ "read" ], Unit.find_by!(device_id: DEVICE_ID).firmwares.pluck(:source)
  end

  # A Plus whose haptic driver stops answering its probe reads as a Go from that boot on.
  test "a unit read on another variant opens a stretch on it, and the change is journaled" do
    hear read(variant: "plus", at: 2.hours.ago)

    assert_event_reported "unit.variant_changed", payload: { from: "plus", to: "standard", version: "0.4.0+1400", source: "read" } do
      hear read(variant: "standard", version: "0.4.0+1400")
    end
    assert_equal [ "standard", "plus" ], Unit.find_by!(device_id: DEVICE_ID).firmwares.newest_first.pluck(:variant)
  end

  # A browser can post any integer, and one past SQLite's 8 bytes would raise and stop the rest of the journal being heard.
  test "an install that claims to have lasted longer than a day is ignored, and the events after it are still heard" do
    hear read(version: "0.4.0+1400", at: 2.hours.ago),
      entry("device.installed", device_id: DEVICE_ID, from: "0.3.0+1234", to: "0.4.0+1400", lasted_ms: 2**63),
      read(version: "0.5.0+1500", at: 1.hour.ago)

    firmwares = Unit.find_by!(device_id: DEVICE_ID).firmwares.newest_first
    assert_equal [ [ "0.5.0+1500", "read", nil ], [ "0.4.0+1400", "read", nil ] ], firmwares.pluck(:version, :source, :lasted_ms)
  end

  test "a report that names no valid identity records nothing" do
    hear read(device_id: "nope"), read(product: "tango"), read(variant: nil), read(bluetooth_tag: 0x10000)

    assert_nil Unit.find_by(device_id: DEVICE_ID)
    assert_equal 3, Unit.count
  end

  test "a unit already known is read again whatever its network, and a network past its cap records no new unit" do
    with_memory_cache do
      Unit::NEW_REPORTED_PER_NETWORK_PER_DAY.times do |index|
        hear read(device_id: format("%016x", 0xe000 + index)), ip_address: "203.0.113.9"
      end

      assert_no_difference -> { Unit.count } do
        hear read, ip_address: "203.0.113.9"
      end
      hear read(device_id: format("%016x", 0xe000), version: "0.4.0+1400"), ip_address: "203.0.113.9"
      assert_equal 2, Unit.find_by!(device_id: format("%016x", 0xe000)).firmwares.count
    end
  end

  test "a reported unit is found by its slug, a Go by its address once activated and by its tag before" do
    assert_equal [ units(:bravo) ], Unit.with_slug("bravo-3f2a").to_a
    assert_equal [ units(:bound) ], Unit.with_slug("go-000001").to_a
    hear read(product: "go", bluetooth_tag: 0x3f2a)
    assert_equal [ DEVICE_ID ], Unit.with_slug("activate-3f2a").pluck(:device_id)
    assert_empty Unit.with_slug("tango-3f2a")
  end

  private
    def hear(*events, ip_address: nil) = Unit.hear_now(events.map(&:uuid), ip_address:)

    def read(at: 2.hours.ago, **identity)
      entry("device.read", at:, device_id: DEVICE_ID, product: "bravo", variant: "standard", version: "0.3.0+1234", release: true,
        bluetooth_tag: 0x0abc, **identity)
    end

    def entry(name, at: Time.current, **payload)
      Event.create!(uuid: SecureRandom.uuid, name:, payload:, occurred_at: at)
    end
end
