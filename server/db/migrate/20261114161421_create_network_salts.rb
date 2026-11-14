class CreateNetworkSalts < ActiveRecord::Migration[8.1]
  def change
    create_table :network_salts do |t|
      t.date :day, null: false
      t.binary :secret, null: false

      t.timestamps
    end
    add_index :network_salts, :day, unique: true
  end
end
