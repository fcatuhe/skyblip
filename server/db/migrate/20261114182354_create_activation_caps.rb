class CreateActivationCaps < ActiveRecord::Migration[8.1]
  def change
    create_table :activation_caps do |t|
      t.integer :new_units_per_day, null: false, default: 50

      t.timestamps
    end
    add_check_constraint :activation_caps, "new_units_per_day > 0", name: "activation_caps_new_units_positive"
  end
end
