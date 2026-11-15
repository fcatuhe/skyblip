class RemoveAddressColumns < ActiveRecord::Migration[8.1]
  def change
    remove_index :units, %i[enrolled_from created_at]
    remove_column :units, :enrolled_from, :string
    remove_column :unit_keys, :ip_address, :string
    remove_column :activation_challenges, :ip_address, :string
  end
end
