class CreateUnitKeys < ActiveRecord::Migration[8.1]
  def change
    create_table :unit_keys do |t|
      t.references :unit, null: false, foreign_key: true, index: false
      t.string :public_key, null: false
      t.string :status, null: false
      t.string :ip_address
      t.string :version
      t.string :server_key

      t.timestamps
    end
    add_index :unit_keys, %i[unit_id public_key], unique: true
    add_index :unit_keys, :unit_id, unique: true, where: "status = 'active'", name: "index_unit_keys_on_unit_id_active"
  end
end
