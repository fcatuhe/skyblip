class ForgetVisitAddresses < ActiveRecord::Migration[8.1]
  class Visit < AnalyticsRecord
    self.table_name = "ahoy_visits"
  end

  def up
    Visit.where.not(ip: nil).or(Visit.where.not(user_agent: nil)).update_all(ip: nil, user_agent: nil)
  end

  def down
  end
end
