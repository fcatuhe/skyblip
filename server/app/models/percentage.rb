module Percentage
  DECIMALS = 2

  def self.of(part, whole)
    if whole.zero?
      0.0
    else
      (part * 100.0 / whole).round(DECIMALS)
    end
  end
end
