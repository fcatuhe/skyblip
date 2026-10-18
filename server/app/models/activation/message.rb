module Activation::Message
  extend self

  def register(unit_nonce:, server_nonce:)
    label("register") + bytes(unit_nonce, server_nonce)
  end

  def registration(device_id:, public_key:, unit_nonce:, server_nonce:, server_key:, release:, version:)
    label("registration") + bytes(device_id, public_key, unit_nonce, server_nonce, server_key) + [ release ? 1 : 0 ].pack("C") + version.b
  end

  def unlock(device_id:, public_key:, serial:, unit_nonce:)
    label("unlock") + bytes(device_id, public_key) + [ serial ].pack("n") + bytes(unit_nonce)
  end

  private
    def label(purpose) = "skyblip:#{purpose}:v1".b

    def bytes(*hex) = [ hex.join ].pack("H*")
end
