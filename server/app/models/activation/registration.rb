class Activation::Registration
  include ActiveModel::API
  include ActiveModel::Attributes

  attribute :device_id, :string
  attribute :public_key, :string
  attribute :version, :string
  attribute :server_key, :string
  attribute :release, :boolean
  attribute :server_nonce, :string
  attribute :signature, :string
  attribute :ip_address, :string

  validates :device_id, format: { with: Activation::DEVICE_ID }
  validates :public_key, format: { with: Activation::PUBLIC_KEY }
  validates :version, format: { with: Activation::VERSION }
  validates :server_key, format: { with: Activation::SERVER_KEY }
  validates :release, inclusion: { in: [ true, false ] }
  validates :server_nonce, format: { with: Activation::NONCE }
  validates :signature, format: { with: Activation::SIGNATURE }

  def unlock_code
    raise Activation::Refusal.new(:invalid) if invalid?

    challenge = Activation::Challenge.find_live(server_nonce)
    raise Activation::Refusal.new(:invalid) unless challenge.server_key == server_key
    raise Activation::Refusal.new(:bad_signature) unless signed_by_unit?(challenge.unit_nonce)
    challenge.spend

    key = nil
    # INFO: fc 18oct26 signed inside the transaction that bound the key, so an admin's revocation lands before or after, never between
    code = Unit.transaction do
      key = Unit.register(device_id:, public_key:, version:, server_key:, release:, ip_address:)
      unlock_code_for(key.unit.serial, challenge.unit_nonce) if key.active?
    end
    code || raise(Activation::Refusal.new(key.revoked? ? :revoked_key : :other_key))
  end

  private
    def signed_by_unit?(unit_nonce)
      message = Activation::Message.registration(device_id:, public_key:, unit_nonce:, server_nonce:, server_key:, release:, version:)
      unit_key = OpenSSL::PKey.new_raw_public_key("ED25519", [ public_key ].pack("H*"))
      unit_key.verify(nil, [ signature ].pack("H*"), message)
    end

    def unlock_code_for(serial, unit_nonce)
      message = Activation::Message.unlock(device_id:, public_key:, serial:, unit_nonce:)
      { serial:, signature: Activation::ServerKey.find(server_key).signature_for(message) }
    end
end
