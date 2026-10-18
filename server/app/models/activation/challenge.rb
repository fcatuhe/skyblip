class Activation::Challenge < ApplicationRecord
  LIFETIME = 5.minutes

  scope :live, -> { where(created_at: LIFETIME.ago..) }
  scope :expired, -> { where(created_at: ...LIFETIME.ago) }

  validates :unit_nonce, format: { with: Activation::NONCE }
  validates :server_nonce, format: { with: Activation::NONCE }

  before_validation(on: :create) { self.server_nonce ||= SecureRandom.hex(16) }

  def self.issue(attributes)
    Activation::ServerKey.find(attributes[:server_key])
    create!(attributes)
  rescue ActiveRecord::RecordInvalid
    raise Activation::Refusal.new(:invalid)
  end

  def self.redeem(server_nonce)
    challenge = live.find_by(server_nonce:)
    # INFO: fc 18oct26 a server nonce counts once: only the request whose delete removed the row may use it
    if challenge && where(id: challenge.id).delete_all == 1
      challenge
    else
      raise Activation::Refusal.new(:expired)
    end
  end

  def signature
    Activation::ServerKey.find(server_key).signature_for(Activation::Message.register(unit_nonce:, server_nonce:))
  end
end
