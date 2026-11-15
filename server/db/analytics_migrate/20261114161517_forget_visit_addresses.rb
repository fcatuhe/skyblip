class ForgetVisitAddresses < ActiveRecord::Migration[8.1]
  class Visit < AnalyticsRecord
    self.table_name = "ahoy_visits"
  end

  def up
    Visit.where.not(ip: nil).update_all(ip: nil)
  end

  def down
  end
end
