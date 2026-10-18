class CreateActivationChallenges < ActiveRecord::Migration[8.1]
  def change
    create_table :activation_challenges do |t|
      t.string :server_nonce, null: false
      t.string :unit_nonce, null: false
      t.string :server_key, null: false
      t.string :ip_address

      t.timestamps
    end
    add_index :activation_challenges, :server_nonce, unique: true
    add_index :activation_challenges, :created_at
  end
end
