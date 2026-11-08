class AddEnrolledFromToUnits < ActiveRecord::Migration[8.1]
  def change
    add_column :units, :enrolled_from, :string
    add_index :units, %i[enrolled_from created_at]
  end
end
