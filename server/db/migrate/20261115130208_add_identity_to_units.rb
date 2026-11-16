class AddIdentityToUnits < ActiveRecord::Migration[8.1]
  def up
    add_column :units, :product, :string
    add_column :units, :variant, :string
    add_column :units, :bluetooth_tag, :integer
    add_column :units, :activated_at, :datetime
    change_column_null :units, :serial, true
    add_index :units, :activated_at
    add_index :units, %i[product bluetooth_tag]
  end

  # INFO: fc 15nov26 a unit with no serial cannot go back under the old NOT NULL, and only an admin may decide to drop it
  def down
    raise ActiveRecord::IrreversibleMigration, "units without a serial exist" if select_value("SELECT 1 FROM units WHERE serial IS NULL LIMIT 1")

    remove_index :units, %i[product bluetooth_tag]
    remove_index :units, :activated_at
    change_column_null :units, :serial, false
    remove_column :units, :activated_at
    remove_column :units, :bluetooth_tag
    remove_column :units, :variant
    remove_column :units, :product
  end
end
