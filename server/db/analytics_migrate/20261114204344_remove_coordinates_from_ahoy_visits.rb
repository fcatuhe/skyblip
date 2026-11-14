class RemoveCoordinatesFromAhoyVisits < ActiveRecord::Migration[8.1]
  def change
    remove_column :ahoy_visits, :latitude, :float
    remove_column :ahoy_visits, :longitude, :float
  end
end
