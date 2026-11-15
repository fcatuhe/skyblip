class CreateAttestationChallenges < ActiveRecord::Migration[8.1]
  def change
    create_table :attestation_challenges do |t|
      t.string :server_nonce, null: false

      t.timestamps
    end
    add_index :attestation_challenges, :server_nonce, unique: true
    add_index :attestation_challenges, :created_at
  end
end
