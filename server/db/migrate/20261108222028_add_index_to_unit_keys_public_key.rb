class AddIndexToUnitKeysPublicKey < ActiveRecord::Migration[8.1]
  def change
    add_index :unit_keys, :public_key
  end
end
