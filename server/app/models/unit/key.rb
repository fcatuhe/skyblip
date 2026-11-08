class Unit::Key < ApplicationRecord
  belongs_to :unit

  enum :status, %w[active pending revoked].index_by(&:itself)

  # INFO: fc 08nov26 a key on two DEVICEIDs is a cloned key or a failing RNG, so the admin pages show it
  scope :shared, -> { where(public_key: Unit::Key.group(:public_key).having(arel_table[:unit_id].count(true).gt(1)).select(:public_key)) }

  validates :public_key, format: { with: Activation::PUBLIC_KEY }

  def other_units = Unit.where(id: Unit::Key.where(public_key:).where.not(unit_id:).select(:unit_id)).order(:serial)

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
