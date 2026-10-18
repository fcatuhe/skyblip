require "test_helper"

class Activation::ServerKeyTest < ActiveSupport::TestCase
  test "names a key by the first four bytes of the SHA-256 of its public key" do
    key = Activation::ServerKey.find("890026b1")

    assert_equal "test", key.name
    assert_equal vectors["server_public_key"], key.public_key
  end

  test "refuses a key this server does not hold" do
    refusal = assert_raises(Activation::Refusal) { Activation::ServerKey.find("a9649d2d") }

    assert_equal :unknown_server_key, refusal.reason
  end

  test "fails loudly when the credentials hold no key at all" do
    keys = Rails.configuration.x.activation.server_keys
    Rails.configuration.x.activation.server_keys = {}

    assert_raises(KeyError) { Activation::ServerKey.find("890026b1") }
  ensure
    Rails.configuration.x.activation.server_keys = keys
  end
end
