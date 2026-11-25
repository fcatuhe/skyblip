class Activation::Registration
  include ActiveModel::API
  include ActiveModel::Attributes

  attribute :device_id, :string
  attribute :public_key, :string
  attribute :version, :string
  attribute :server_key_id, :string
  attribute :release
  attribute :product, :string
  attribute :variant, :string
  attribute :bluetooth_tag
  attribute :server_nonce, :string
  attribute :signature, :string
  attribute :ip_address, :string

  validates :device_id, format: { with: Activation::DEVICE_ID }
  validates :public_key, format: { with: Activation::PUBLIC_KEY }
  validates :version, format: { with: Activation::VERSION }
  validates :server_key_id, format: { with: Activation::SERVER_KEY_ID }
  validates :server_nonce, format: { with: Activation::NONCE }
  validates :signature, format: { with: Activation::SIGNATURE }
  validates :product, inclusion: { in: Identity::Message::PRODUCTS.keys }, if: :named?
  validates :variant, inclusion: { in: Identity::Message::VARIANTS.keys }, if: :named?
  validate :bluetooth_tag_a_tag, if: :named?
  validate :release_a_boolean, :public_key_strong

  attr_reader :unit

  def unlock_code
    raise Activation::Refusal.new(:invalid) if invalid?

    challenge = Activation::Challenge.find_live(server_nonce)
    raise Activation::Refusal.new(:invalid) unless challenge.server_key_id == server_key_id
    raise Activation::Refusal.new(:bad_signature) unless signed_by_unit?(challenge.unit_nonce)
    challenge.spend

    key = nil
    # INFO: fc 18oct26 signed inside the transaction that bound the key, so an admin's revocation lands before or after, never between
    code = Unit.transaction do
      key = Unit.register(device_id:, public_key:, version:, server_key_id:, release:, ip_address:)
      @unit = key.unit
      if key.active?
        key.unit.attest_firmware(version:, release:, **named_unit)
        unlock_code_for(key.unit.serial, challenge.unit_nonce)
      end
    end
    code || raise(Activation::Refusal.new(key.revoked? ? :revoked_key : :other_key))
  end

  private
    def named? = [ product, variant, bluetooth_tag ].any?

    def named_unit = named? ? { product:, variant:, bluetooth_tag: } : {}

    # INFO: fc 09nov26 the inclusion validator checks each element of an array, so [true] would pass as a release
    def release_a_boolean
      errors.add(:release, :inclusion) unless release == true || release == false
    end

    def bluetooth_tag_a_tag
      errors.add(:bluetooth_tag, :inclusion) unless bluetooth_tag.is_a?(Integer) && Unit::BLUETOOTH_TAGS.cover?(bluetooth_tag)
    end

    def public_key_strong
      if public_key.to_s.match?(Activation::PUBLIC_KEY) && Identity::PublicKey.weak?(public_key)
        errors.add(:public_key, :invalid)
      end
    end

    def signed_by_unit?(unit_nonce)
      signed = { device_id:, public_key:, unit_nonce:, server_nonce:, server_key_id:, release:, version: }
      message = named? ? Identity::Message.registration(**signed, **named_unit) : Identity::Message.registration_v1(**signed)
      unit_key = OpenSSL::PKey.new_raw_public_key("ED25519", [ public_key ].pack("H*"))
      unit_key.verify(nil, [ signature ].pack("H*"), message)
    end

    def unlock_code_for(serial, unit_nonce)
      message = Identity::Message.unlock(device_id:, public_key:, serial:, unit_nonce:)
      { serial:, signature: Identity::ServerKey.current.signature_for(message) }
    end
end
