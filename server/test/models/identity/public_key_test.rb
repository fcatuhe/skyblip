require "test_helper"

class Identity::PublicKeyTest < ActiveSupport::TestCase
  # libsodium 1.0.18's blocklist, ed25519_ref10.c: every point of order 1, 2, 4 or 8, little-endian, either sign.
  SMALL_ORDER = %w[
    0000000000000000000000000000000000000000000000000000000000000000
    0100000000000000000000000000000000000000000000000000000000000000
    26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc05
    c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a
    ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f
  ]

  test "a key of small order is weak, whichever sign it carries" do
    SMALL_ORDER.each do |key|
      assert Identity::PublicKey.weak?(key), key
      assert Identity::PublicKey.weak?(key[0, 62] + format("%02x", key[62, 2].to_i(16) | 0x80)), key
    end
  end

  test "a key that encodes y at or past p is weak, the aliases of 0 and 1 among them" do
    assert Identity::PublicKey.weak?("ed" + "ff" * 30 + "7f")
    assert Identity::PublicKey.weak?("ee" + "ff" * 30 + "7f")
    assert Identity::PublicKey.weak?("ff" * 32)
  end

  test "the vectors' unit key and the server's are not weak" do
    assert_not Identity::PublicKey.weak?(vectors["unit_public_key"])
    assert_not Identity::PublicKey.weak?(vectors["server_public_key"])
  end
end
