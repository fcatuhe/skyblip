class AddIdentityToUnits < ActiveRecord::Migration[8.1]
  def change
    add_column :units, :product, :string
    add_column :units, :variant, :string
    add_column :units, :bluetooth_tag, :integer
    add_column :units, :activated_at, :datetime
    change_column_null :units, :serial, true
    add_index :units, :activated_at
    add_index :units, %i[product bluetooth_tag]
  end
end
