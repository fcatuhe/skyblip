module ActivationTestHelper
  VECTORS = JSON.parse(Rails.root.join("../schemas/activation.v1.vectors.json").read).freeze

  def vectors = VECTORS

  def unit_key(seed = vectors["unit_seed"]) = OpenSSL::PKey.new_raw_private_key("ED25519", [ seed ].pack("H*"))

  def signed_registration(challenge, unit_key: self.unit_key, device_id: vectors["device_id"], version: vectors["version"])
    public_key = unit_key.raw_public_key.unpack1("H*")
    fields = { device_id:, public_key:, server_nonce: challenge.server_nonce, server_key: challenge.server_key, version: }
    message = Activation::Message.registration(**fields, unit_nonce: challenge.unit_nonce)
    fields.merge(signature: unit_key.sign(nil, message).unpack1("H*"))
  end

  def server_verifies?(signature, message)
    server_key = OpenSSL::PKey.new_raw_public_key("ED25519", [ vectors["server_public_key"] ].pack("H*"))
    server_key.verify(nil, [ signature ].pack("H*"), message)
  end
end

ActiveSupport.on_load(:active_support_test_case) do
  include ActivationTestHelper
end
