class Unit::Firmware < ApplicationRecord
  belongs_to :unit

  scope :newest_first, -> { order(created_at: :desc, id: :desc) }

  validates :version, format: { with: Activation::VERSION }
  validates :release, inclusion: { in: [ true, false ] }
end
