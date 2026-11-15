class RecordActivationOfUnits < ActiveRecord::Migration[8.1]
  class Unit < ActiveRecord::Base; end

  # INFO: fc 15nov26 only a Go could be activated until now, so every unit is a Go activated when it was created
  def up
    Unit.update_all("activated_at = created_at, product = 'go'")
  end

  def down
  end
end
