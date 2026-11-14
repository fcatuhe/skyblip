class CreateOAuthGrants < ActiveRecord::Migration[8.1]
  def change
    create_table :oauth_grants do |t|
      t.references :user, null: false, foreign_key: true, index: false
      t.references :client, null: false, foreign_key: { to_table: :oauth_clients }
      t.string :scope, null: false
      t.string :resource, null: false
      t.datetime :approved_at, null: false, index: true
      t.datetime :last_used_at

      t.timestamps
    end
    add_index :oauth_grants, %i[user_id client_id], unique: true
  end
end
