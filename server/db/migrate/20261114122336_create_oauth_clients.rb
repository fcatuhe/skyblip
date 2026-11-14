class CreateOAuthClients < ActiveRecord::Migration[8.1]
  def change
    create_table :oauth_clients do |t|
      t.string :document_url, null: false, index: { unique: true }
      t.string :name, null: false
      t.string :client_uri
      t.json :redirect_uris, null: false
      t.datetime :fetched_at, null: false

      t.timestamps
    end
  end
end
