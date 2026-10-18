require "test_helper"

class Identity::ServerKeyTest < ActiveSupport::TestCase
  test "names the key by the first four bytes of the SHA-256 of its public key" do
    key = Identity::ServerKey.current

    assert_equal [ "890026b1", vectors["server_public_key"] ], [ key.id, key.public_key ]
    assert_equal key, Identity::ServerKey.named("890026b1")
    assert_nil Identity::ServerKey.named("a9649d2d")
  end

  test "fails loudly when the credentials hold no key" do
    seed = Rails.configuration.x.identity.server_key
    Rails.configuration.x.identity.server_key = nil

    assert_not Identity::ServerKey.configured?
    assert_raises(KeyError) { Identity::ServerKey.current }
  ensure
    Rails.configuration.x.identity.server_key = seed
  end
end
