class CreateEvents < ActiveRecord::Migration[8.1]
  def change
    create_table :events do |t|
      t.string :name, null: false
      t.json :payload, null: false, default: {}
      t.json :context, null: false, default: {}
      t.string :source
      t.string :uuid
      t.integer :user_id
      t.string :request_id
      t.virtual :device, type: :string, as: "coalesce(json_extract(payload, '$.device'), json_extract(context, '$.device'))"
      t.virtual :browser, type: :string, as: "json_extract(context, '$.browser')"
      t.virtual :fingerprint, type: :string, as: "json_extract(payload, '$.fingerprint')"
      t.datetime :occurred_at, null: false
    end
    add_index :events, %i[name occurred_at]
    add_index :events, %i[device occurred_at]
    add_index :events, %i[browser occurred_at]
    add_index :events, %i[fingerprint occurred_at]
    add_index :events, %i[user_id occurred_at]
    add_index :events, :request_id
    add_index :events, :occurred_at
    add_index :events, :uuid, unique: true
  end
end
