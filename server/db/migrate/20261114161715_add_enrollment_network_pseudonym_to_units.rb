class AddEnrollmentNetworkPseudonymToUnits < ActiveRecord::Migration[8.1]
  def change
    add_column :units, :enrollment_network_pseudonym, :string
    add_index :units, %i[enrollment_network_pseudonym created_at]
  end
end
