class Activation::Cap < ApplicationRecord
  validates :new_units_per_day, numericality: { only_integer: true, greater_than: 0 }

  after_update_commit -> { Rails.event.notify "activation_cap.changed", from: new_units_per_day_previously_was, to: new_units_per_day, by_id: Current.user&.id }

  def self.current = order(:id).first_or_create!
end
