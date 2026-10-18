module Scale
  STEPS = [ 1, 1.5, 2, 2.5, 3, 4, 5, 10 ].freeze

  def self.ceiling(peak)
    if peak.zero?
      0
    else
      magnitude = 10**Math.log10(peak).floor
      (STEPS.find { peak <= it * magnitude } * magnitude).round
    end
  end
end
