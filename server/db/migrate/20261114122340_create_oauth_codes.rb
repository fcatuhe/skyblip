class CreateOAuthCodes < ActiveRecord::Migration[8.1]
  def change
    create_table :oauth_codes do |t|
      t.references :grant, null: false, foreign_key: { to_table: :oauth_grants, on_delete: :cascade }
      t.string :digest, null: false, index: { unique: true }
      t.string :code_challenge, null: false
      t.string :redirect_uri, null: false
      t.datetime :expires_at, null: false, index: true
      t.datetime :redeemed_at

      t.timestamps
    end
  end
end
