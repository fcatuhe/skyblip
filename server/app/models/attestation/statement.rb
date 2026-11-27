class Attestation::Statement
  include ActiveModel::API
  include ActiveModel::Attributes

  attribute :device_id, :string
  attribute :product, :string
  attribute :variant, :string
  attribute :version, :string
  attribute :release
  attribute :server_nonce, :string
  attribute :signature, :string

  validates :device_id, format: { with: Activation::DEVICE_ID }
  validates :product, inclusion: { in: Identity::Message::PRODUCTS.keys }
  validates :variant, inclusion: { in: Identity::Message::VARIANTS.keys }
  validates :version, format: { with: Activation::VERSION }
  validates :server_nonce, format: { with: Activation::NONCE }
  validates :signature, format: { with: Activation::SIGNATURE }
  validate :release_a_boolean

  def record
    raise Attestation::Refusal.new(:invalid) if invalid?

    challenge = Attestation::Challenge.find_live(server_nonce)
    unit = Unit.find_by(device_id:) || raise(Attestation::Refusal.new(:unknown_unit))

    # INFO: fc 26nov26 checked inside the transaction that records the firmware, so an admin's revocation lands before or after, never between
    Unit.transaction do
      key = unit.active_key || raise(Attestation::Refusal.new(:no_active_key))
      raise Attestation::Refusal.new(:bad_signature) unless signed_by?(key)
      challenge.spend
      unit.attest_firmware(product:, variant:, version:, release:)
    end
  end

  private
    # INFO: fc 14nov26 the inclusion validator checks each element of an array, so [true] would pass as a release
    def release_a_boolean
      errors.add(:release, :inclusion) unless release == true || release == false
    end

    def signed_by?(key)
      message = Identity::Message.attestation(device_id:, public_key: key.public_key, server_nonce:, release:, product:, variant:, version:)
      unit_key = OpenSSL::PKey.new_raw_public_key("ED25519", [ key.public_key ].pack("H*"))
      unit_key.verify(nil, [ signature ].pack("H*"), message)
    end
end
