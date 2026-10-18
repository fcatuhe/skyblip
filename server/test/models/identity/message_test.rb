require "test_helper"

class Identity::MessageTest < ActiveSupport::TestCase
  test "builds the three messages byte for byte as the vectors the firmware checks" do
    assert_equal vectors["register_message"], hex(Identity::Message.register(unit_nonce: vectors["unit_nonce"], server_nonce: vectors["server_nonce"]))
    assert_equal vectors["registration_message"], hex(Identity::Message.registration(**registration_fields))
    assert_equal vectors["unlock_message"], hex(Identity::Message.unlock(**unlock_fields))
  end

  # Ed25519 is deterministic, so equal signatures mean OpenSSL and Monocypher sign alike.
  test "OpenSSL signs the server's two messages as the firmware does, and verifies the unit's" do
    server_key = Identity::ServerKey.current

    assert_equal vectors["register_signature"], server_key.signature_for(bytes(vectors["register_message"]))
    assert_equal vectors["unlock_signature"], server_key.signature_for(bytes(vectors["unlock_message"]))
    assert unit_public_key.verify(nil, bytes(vectors["registration_signature"]), bytes(vectors["registration_message"]))
  end

  test "a signature for one purpose does not verify as another" do
    assert_not unit_public_key.verify(nil, bytes(vectors["registration_signature"]), bytes(vectors["unlock_message"]))
    assert_not server_verifies?(vectors["unlock_signature"], bytes(vectors["register_message"]))
  end

  private
    def registration_fields
      vectors.slice("device_id", "unit_nonce", "server_nonce", "server_key", "release", "version").symbolize_keys.merge(public_key: vectors["unit_public_key"])
    end

    def unlock_fields
      { device_id: vectors["device_id"], public_key: vectors["unit_public_key"], serial: vectors["serial"], unit_nonce: vectors["unit_nonce"] }
    end

    def unit_public_key = OpenSSL::PKey.new_raw_public_key("ED25519", bytes(vectors["unit_public_key"]))

    def hex(bytes) = bytes.unpack1("H*")

    def bytes(hex) = [ hex ].pack("H*")
end
