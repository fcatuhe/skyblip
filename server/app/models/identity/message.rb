module Identity::Message
  extend self

  # INFO: fc 15nov26 the codes schemas/link.v1.json gives product and variant, one byte each in a signed message
  PRODUCTS = { "go" => 0, "bravo" => 1 }.freeze
  VARIANTS = { "standard" => 0, "plus" => 1 }.freeze

  def register(unit_nonce:, server_nonce:)
    label("register") + bytes(unit_nonce, server_nonce)
  end

  def registration(device_id:, public_key:, unit_nonce:, server_nonce:, server_key_id:, release:, product:, variant:, bluetooth_tag:, version:)
    label("registration", 2) + bytes(device_id, public_key, unit_nonce, server_nonce, server_key_id) +
      [ release ? 1 : 0, PRODUCTS.fetch(product), VARIANTS.fetch(variant), bluetooth_tag ].pack("C3n") + version.b
  end

  # INFO: fc 25nov26 firmware as old as v0.8.0 signs this one, so it stays until activation refuses that firmware
  def registration_v1(device_id:, public_key:, unit_nonce:, server_nonce:, server_key_id:, release:, version:)
    label("registration") + bytes(device_id, public_key, unit_nonce, server_nonce, server_key_id) + [ release ? 1 : 0 ].pack("C") + version.b
  end

  def unlock(device_id:, public_key:, serial:, unit_nonce:)
    label("unlock") + bytes(device_id, public_key) + [ serial ].pack("n") + bytes(unit_nonce)
  end

  def attestation(device_id:, public_key:, server_nonce:, release:, product:, variant:, version:)
    label("attestation") + bytes(device_id, public_key, server_nonce) +
      [ release ? 1 : 0, PRODUCTS.fetch(product), VARIANTS.fetch(variant) ].pack("C3") + version.b
  end

  private
    def label(purpose, version = 1) = "skyblip:#{purpose}:v#{version}".b

    def bytes(*hex) = [ hex.join ].pack("H*")
end
