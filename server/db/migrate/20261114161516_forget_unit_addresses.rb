class ForgetUnitAddresses < ActiveRecord::Migration[8.1]
  class Unit < ActiveRecord::Base; end
  class UnitKey < ActiveRecord::Base; end
  class ActivationChallenge < ActiveRecord::Base; end

  def up
    Unit.where.not(enrolled_from: nil).update_all(enrolled_from: nil)
    UnitKey.where.not(ip_address: nil).update_all(ip_address: nil)
    ActivationChallenge.where.not(ip_address: nil).update_all(ip_address: nil)
  end

  def down
  end
end
