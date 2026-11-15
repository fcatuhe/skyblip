class CreateUnitFirmwares < ActiveRecord::Migration[8.1]
  def change
    create_table :unit_firmwares do |t|
      t.references :unit, null: false, foreign_key: true, index: false
      t.string :version, null: false
      t.boolean :release, null: false

      t.timestamps
    end
    add_index :unit_firmwares, %i[unit_id created_at]
  end
end
