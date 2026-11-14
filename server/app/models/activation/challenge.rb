class Activation::Challenge < ApplicationRecord
  # TODO: fc 14nov26 the column goes once a deploy runs without reading it, then this line goes too
  self.ignored_columns += %w[ip_address]

  include SingleUse

  validates :unit_nonce, format: { with: Activation::NONCE }

  def self.issue(attributes)
    if Identity::ServerKey.named(attributes[:server_key_id])
      create!(attributes)
    else
      raise Activation::Refusal.new(:unknown_server_key)
    end
  rescue ActiveRecord::RecordInvalid
    raise Activation::Refusal.new(:invalid)
  end

  def signature
    Identity::ServerKey.current.signature_for(Identity::Message.register(unit_nonce:, server_nonce:))
  end
end
