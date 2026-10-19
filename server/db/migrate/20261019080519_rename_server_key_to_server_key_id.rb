class RenameServerKeyToServerKeyId < ActiveRecord::Migration[8.1]
  def change
    rename_column :activation_challenges, :server_key, :server_key_id
    rename_column :unit_keys, :server_key, :server_key_id
  end
end
