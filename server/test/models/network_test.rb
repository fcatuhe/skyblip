require "test_helper"

class NetworkTest < ActiveSupport::TestCase
  test "names a network by its IPv4 address or its IPv6 /56, a mapped IPv4 address as itself" do
    assert_equal "203.0.113.8", Network.of("203.0.113.8")
    assert_equal "203.0.113.8", Network.of("::ffff:203.0.113.8")
    assert_equal "2001:db8:1::", Network.of("2001:db8:1:2:aaaa:bbbb:cccc:dddd")
    assert_equal "2001:db8:1:100::", Network.of("2001:db8:1:1ff::1")
  end

  # Docker on crooz hands Rails its gateway, 172.18.0.1, for every IPv6 client (#205).
  test "a private address is no network, so it has no pseudonym, and a rate limit counts it by itself" do
    [ "172.18.0.1", "::1", "fe80::1" ].each do |address|
      assert_nil Network.of(address)
      assert_nil Network.pseudonym_of(address)
    end
    assert_not_equal Network.rate_limit_key("172.18.0.1"), Network.rate_limit_key("172.18.0.2")
  end

  test "a network has one pseudonym all day, which names no address, and another the next day" do
    today = Network.pseudonym_of("2001:db8:1:2::1")

    assert_equal today, Network.pseudonym_of("2001:db8:1:ff::9")
    assert_equal today, Network.rate_limit_key("2001:db8:1:2::1")
    assert_not_equal today, Network.pseudonym_of("2001:db8:1:100::1")
    assert_no_match(/2001|db8/, today)
    travel(1.day) { assert_not_equal today, Network.pseudonym_of("2001:db8:1:2::1") }
  end

  # Once its salt is gone, nobody can try every address against an old pseudonym, us included.
  test "a salt is forgotten once its day is over" do
    Network.pseudonym_of("203.0.113.8")

    travel 1.day do
      Network.pseudonym_of("203.0.113.8")
      Network::Salt.expired.delete_all

      assert_equal [ Date.current ], Network::Salt.pluck(:day)
    end
  end
end
