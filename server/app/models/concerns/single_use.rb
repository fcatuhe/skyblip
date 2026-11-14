module SingleUse
  extend ActiveSupport::Concern

  LIFETIME = 5.minutes

  included do
    scope :live, -> { where(created_at: LIFETIME.ago..) }
    scope :expired, -> { where(created_at: ...LIFETIME.ago) }

    validates :server_nonce, format: { with: Activation::NONCE }

    before_validation(on: :create) { self.server_nonce ||= SecureRandom.hex(16) }
  end

  class_methods do
    def find_live(server_nonce)
      live.find_by(server_nonce:) || raise(module_parent::Refusal.new(:expired))
    end
  end

  def spend
    # INFO: fc 18oct26 a server nonce counts once: only the request whose delete removed the row may use it
    if self.class.live.where(id:).delete_all == 1
      self
    else
      raise self.class.module_parent::Refusal.new(:expired)
    end
  end
end
