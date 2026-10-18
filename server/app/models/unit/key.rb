class Unit::Key < ApplicationRecord
  belongs_to :unit

  enum :status, %w[active pending revoked].index_by(&:itself)

  validates :public_key, format: { with: Activation::PUBLIC_KEY }

  def promote
    transaction do
      unit.keys.active.each(&:revoked!)
      active!
    end
  end
end
