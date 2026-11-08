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
    seen = unit_keys(:bound_active).slice(:version, :server_key_id, :release, :ip_address).symbolize_keys

    travel 1.hour do
      key = Unit.register(device_id: units(:bound).device_id, public_key: unit_keys(:bound_active).public_key, **seen)
      assert_in_delta Time.current, key.reload.updated_at, 1.second
    end
  end

  test "a known device with another key is recorded as pending and bound to nothing" do
    key = Unit.register(device_id: units(:bound).device_id, public_key: "e5" * 32, ip_address: "203.0.113.4")

    assert key.pending?
    assert key.persisted?
    assert_equal "203.0.113.4", key.ip_address
    assert_equal unit_keys(:bound_active), units(:bound).active_key
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
    assert_equal [ "203.0.113.5" ] * 2, revoked.map(&:ip_address)
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

  test "refuses a new device past the cap of its network, an IPv6 /64 counting as one, and serves other networks" do
    Unit::NEW_PER_NETWORK_PER_DAY.times do |index|
      Unit.register(device_id: format("%016x", 0xb000 + index), public_key: "d4" * 32, ip_address: "2001:db8:1:2::#{index + 1}")
    end

    refusal = assert_raises(Activation::Refusal) { Unit.register(device_id: vectors["device_id"], public_key: vectors["unit_public_key"], ip_address: "2001:db8:1:2:ffff::9") }
    assert_equal :network_cap, refusal.reason
    assert Unit.register(device_id: vectors["device_id"], public_key: vectors["unit_public_key"], ip_address: "2001:db8:1:3::1").active?
    assert Unit.register(device_id: units(:bound).device_id, public_key: unit_keys(:bound_active).public_key, ip_address: "2001:db8:1:2::1").active?
  end

  test "names a network by its IPv4 address or its IPv6 /64, a mapped IPv4 address as itself" do
    assert_equal "203.0.113.8", Unit.network_of("203.0.113.8")
    assert_equal "203.0.113.8", Unit.network_of("::ffff:203.0.113.8")
    assert_equal "2001:db8:1:2::", Unit.network_of("2001:db8:1:2:aaaa:bbbb:cccc:dddd")
  end

  test "counts a network's new devices for today only" do
    Unit::NEW_PER_NETWORK_PER_DAY.times do |index|
      Unit.register(device_id: format("%016x", 0xb000 + index), public_key: "d4" * 32, ip_address: "203.0.113.8")
    end

    travel 1.day do
      assert Unit.register(device_id: vectors["device_id"], public_key: vectors["unit_public_key"], ip_address: "203.0.113.8").active?
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

  test "the database refuses serial 0 on its own" do
    assert_raises(ActiveRecord::StatementInvalid) { Unit.insert_all([ { device_id: vectors["device_id"], serial: 0 } ]) }
  end

  test "refuses a new device once every serial is taken" do
    units(:contested).update!(serial: 0xFFFF)

    refusal = assert_raises(Activation::Refusal) { Unit.register(device_id: vectors["device_id"], public_key: vectors["unit_public_key"]) }
    assert_equal :no_serial, refusal.reason
  end
end
