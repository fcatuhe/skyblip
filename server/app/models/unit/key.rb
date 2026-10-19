class Unit::Key < ApplicationRecord
  belongs_to :unit

  enum :status, %w[active pending revoked].index_by(&:itself)

  validates :public_key, format: { with: Activation::PUBLIC_KEY }

  # INFO: fc 19oct26 with_lock reloads the status, so a request loaded before another's change acts on the key as it is now
  def promote
    with_lock do
      if pending?
        unit.keys.active.each(&:revoked!)
        active!
      end
    end
  end

  def revoke
    with_lock do
      revoked! if active?
    end
  end
end
