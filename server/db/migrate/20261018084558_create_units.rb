class CreateUnits < ActiveRecord::Migration[8.1]
  def change
    create_table :units do |t|
      t.string :device_id, null: false
      t.integer :serial, null: false

      t.timestamps
    end
    add_index :units, :device_id, unique: true
    add_index :units, :serial, unique: true
    add_check_constraint :units, "serial BETWEEN 0 AND 65535", name: "units_serial_in_sixteen_bits"
  end
end
