class CreateCaptures < ActiveRecord::Migration[8.1]
  def change
    create_table :captures do |t|
      t.string :device, null: false
      t.string :browser
      t.string :profile, null: false
      t.integer :session, null: false
      t.integer :records, null: false
      t.integer :bytes, null: false
      t.string :firmware
      t.binary :body, null: false
      t.datetime :kept_until, null: false
      t.timestamps
    end
    add_index :captures, [ :device, :created_at ]
    add_index :captures, :browser
    add_index :captures, :kept_until
  end
end
