class RemoveIpFromAhoyVisits < ActiveRecord::Migration[8.1]
  def change
    remove_column :ahoy_visits, :ip, :string
  end
end
