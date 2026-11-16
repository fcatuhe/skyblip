class AddSourceToUnitFirmwares < ActiveRecord::Migration[8.1]
  def change
    # INFO: fc 15nov26 every row so far came from a statement the unit signed
    add_column :unit_firmwares, :source, :string, null: false, default: "attested"
    change_column_default :unit_firmwares, :source, from: "attested", to: nil
    add_column :unit_firmwares, :variant, :string
    add_column :unit_firmwares, :from_version, :string
    add_column :unit_firmwares, :lasted_ms, :integer
  end
end
