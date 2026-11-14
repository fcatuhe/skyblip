class CreateOAuthTokens < ActiveRecord::Migration[8.1]
  def change
    create_table :oauth_tokens do |t|
      t.references :grant, null: false, foreign_key: { to_table: :oauth_grants, on_delete: :cascade }
      t.string :kind, null: false
      t.string :digest, null: false, index: { unique: true }
      t.datetime :expires_at, null: false, index: true
      t.datetime :spent_at

      t.timestamps
    end
  end
end
