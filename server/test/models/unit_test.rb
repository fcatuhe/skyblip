require "test_helper"

class UnitTest < ActiveSupport::TestCase
  test "a new device gets the next serial, densely from 1, and its key is bound" do
    key = Unit.register(device_id: vectors["device_id"], public_key: vectors["unit_public_key"], version: "0.3.0+1234")
    other = Unit.register(device_id: "1111111111111111", public_key: "d4" * 32)

    assert key.active?
    assert_equal [ 3, 4 ], [ key.unit.serial, other.unit.serial ]
    assert_equal "000003", key.unit.address
  end

  test "a known device with its own key keeps its serial" do
    key = Unit.register(device_id: units(:bound).device_id, public_key: unit_keys(:bound_active).public_key, version: "0.3.0+1300")

    assert_equal unit_keys(:bound_active), key
    assert key.active?
    assert_equal "0.3.0+1300", key.reload.version
    assert_equal 1, key.unit.serial
  end

  # The admin page reads a key's updated_at as when it was last seen.
  test "a known device registering again with nothing new is seen again" do
    seen = unit_keys(:bound_active).slice(:version, :server_key_id, :release).symbolize_keys

    travel 1.hour do
      key = Unit.register(device_id: units(:bound).device_id, public_key: unit_keys(:bound_active).public_key, **seen)
      assert_in_delta Time.current, key.reload.updated_at, 1.second
    end
  end

  test "a known device with another key is recorded as pending and bound to nothing" do
    key = Unit.register(device_id: units(:bound).device_id, public_key: "e5" * 32, ip_address: "203.0.113.4")

    assert key.pending?
    assert key.persisted?
    assert_equal unit_keys(:bound_active), units(:bound).active_key
  end

  test "keeps the newest pending keys of a device and forgets the older ones, its active key untouched" do
    keys = Array.new(Unit::PENDING_KEYS_KEPT + 2) do |index|
      travel index.minutes do
        Unit.register(device_id: units(:bound).device_id, public_key: format("%064x", 0xe000 + index))
      end
    end

    assert_equal keys.last(Unit::PENDING_KEYS_KEPT).map(&:public_key).sort, units(:bound).keys.pending.pluck(:public_key).sort
    assert_equal unit_keys(:bound_active), units(:bound).reload.active_key
  end

  test "once its key is revoked, a device binds the next key that registers" do
    unit_keys(:bound_active).revoked!

    key = Unit.register(device_id: units(:bound).device_id, public_key: "e5" * 32)

    assert key.active?
    assert_equal 1, key.unit.serial
  end

  test "a revoked key stays revoked, after a revocation and after a promotion" do
    unit_keys(:bound_active).revoked!
    unit_keys(:contested_pending).promote

    revoked = [ units(:bound), units(:contested) ].map do |unit|
      Unit.register(device_id: unit.device_id, public_key: unit.keys.revoked.first.public_key, ip_address: "203.0.113.5")
    end

    assert revoked.all?(&:revoked?)
    assert_nil units(:bound).reload.active_key
  end

  test "a promoted key replaces the active one and keeps the serial" do
    unit_keys(:contested_pending).promote

    assert unit_keys(:contested_pending).reload.active?
    assert unit_keys(:contested_active).reload.revoked?
    assert_equal unit_keys(:contested_pending), units(:contested).reload.active_key
  end

  test "refuses new devices past the daily cap, and still serves known ones" do
    Unit.insert_all(Array.new(Unit::NEW_PER_DAY) { |index| { device_id: format("%016x", 0xa000 + index), serial: 100 + index } })

    refusal = assert_raises(Activation::Refusal) { Unit.register(device_id: vectors["device_id"], public_key: vectors["unit_public_key"]) }
    assert_equal :daily_cap, refusal.reason
    assert Unit.register(device_id: units(:bound).device_id, public_key: unit_keys(:bound_active).public_key).active?
  end

  test "refuses a new device past the cap of its network, an IPv6 /56 counting as one, and serves other networks" do
    with_memory_cache do
      Unit::NEW_PER_NETWORK_PER_DAY.times do |index|
        Unit.register(device_id: format("%016x", 0xb000 + index), public_key: "d4" * 32, ip_address: "2001:db8:1:2::#{index + 1}")
      end

      refusal = assert_raises(Activation::Refusal) { Unit.register(device_id: vectors["device_id"], public_key: vectors["unit_public_key"], ip_address: "2001:db8:1:ff:ffff::9") }
      assert_equal :network_cap, refusal.reason
      assert Unit.register(device_id: vectors["device_id"], public_key: vectors["unit_public_key"], ip_address: "2001:db8:1:100::1").active?
      assert Unit.register(device_id: units(:bound).device_id, public_key: unit_keys(:bound_active).public_key, ip_address: "2001:db8:1:2::1").active?
    end
  end

  test "names a network by its IPv4 address or its IPv6 /56, a mapped IPv4 address as itself" do
    assert_equal "203.0.113.8", Unit.network_of("203.0.113.8")
    assert_equal "203.0.113.8", Unit.network_of("::ffff:203.0.113.8")
    assert_equal "2001:db8:1::", Unit.network_of("2001:db8:1:2:aaaa:bbbb:cccc:dddd")
    assert_equal "2001:db8:1:100::", Unit.network_of("2001:db8:1:1ff::1")
  end

  # Docker on crooz hands Rails its gateway, 172.18.0.1, for every IPv6 client (#205).
  test "counts a private address toward no network, so the clients behind it do not share one cap" do
    assert_nil Unit.network_of("172.18.0.1")
    assert_nil Unit.network_of("::1")
    assert_nil Unit.network_of("fe80::1")

    with_memory_cache do
      (Unit::NEW_PER_NETWORK_PER_DAY + 1).times do |index|
        assert Unit.register(device_id: format("%016x", 0xc000 + index), public_key: "d4" * 32, ip_address: "172.18.0.1").active?
      end
    end
  end

  test "counts a network's new devices for 24 hours from its first, in the cache and not on the units" do
    with_memory_cache do
      Unit::NEW_PER_NETWORK_PER_DAY.times do |index|
        Unit.register(device_id: format("%016x", 0xb000 + index), public_key: "d4" * 32, ip_address: "203.0.113.8")
      end
      assert_not Unit.new.has_attribute?(:enrolled_from)

      travel 23.hours do
        assert_raises(Activation::Refusal) { Unit.register(device_id: vectors["device_id"], public_key: vectors["unit_public_key"], ip_address: "203.0.113.8") }
      end
      travel 24.hours + 1.second do
        assert Unit.register(device_id: vectors["device_id"], public_key: vectors["unit_public_key"], ip_address: "203.0.113.8").active?
      end
    end
  end

  test "the first unit gets serial 1, never 0" do
    Unit.destroy_all

    assert_equal 1, Unit.register(device_id: vectors["device_id"], public_key: vectors["unit_public_key"]).unit.serial
  end

  # Two admin requests can load the same key; the later one must act on it as it is now.
  test "a promotion loaded before a revocation promotes nothing" do
    stale = Unit::Key.find(unit_keys(:contested_pending).id)
    unit_keys(:contested_pending).promote
    unit_keys(:contested_pending).revoke

    assert_not stale.promote
    assert unit_keys(:contested_pending).reload.revoked?
    assert_nil units(:contested).reload.active_key
  end

  # A development unit may go back to an older image, and its history must say so rather than fold it into the first stretch.
  test "a firmware signed again extends its stretch, and a unit back on an earlier one opens a new stretch" do
    unit = units(:bound)
    unit.attest_firmware(version: "0.3.0+1300", release: true)
    unit.attest_firmware(version: "0.4.0+1400", release: true)
    unit.attest_firmware(version: "0.4.0+1400", release: false)
    unit.attest_firmware(version: "0.3.0+1300", release: true)

    assert_equal [ [ "0.3.0+1300", true ], [ "0.4.0+1400", false ], [ "0.4.0+1400", true ], [ "0.3.0+1300", true ] ],
      unit.firmwares.newest_first.pluck(:version, :release)
    assert_equal Time.current.to_date, unit.firmwares.newest_first.last.updated_at.to_date
  end

  test "a unit that has not signed its firmware since it registered shows the one it registered with" do
    assert_equal "0.3.0+1300", units(:bound).signed_firmware.version
    assert_equal "0.3.0+1200", units(:contested).signed_firmware.version
  end

  test "the database refuses serial 0 on its own" do
    assert_raises(ActiveRecord::StatementInvalid) { Unit.insert_all([ { device_id: vectors["device_id"], serial: 0 } ]) }
  end

  test "refuses a new device once every serial is taken" do
    units(:contested).update!(serial: 0xFFFF)

    refusal = assert_raises(Activation::Refusal) { Unit.register(device_id: vectors["device_id"], public_key: vectors["unit_public_key"]) }
    assert_equal :no_serial, refusal.reason
  end
end
