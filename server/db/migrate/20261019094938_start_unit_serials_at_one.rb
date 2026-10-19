class StartUnitSerialsAtOne < ActiveRecord::Migration[8.1]
  def change
    remove_check_constraint :units, "serial BETWEEN 0 AND 65535", name: "units_serial_in_sixteen_bits"
    add_check_constraint :units, "serial BETWEEN 1 AND 65535", name: "units_serial_from_one"
  end
end
