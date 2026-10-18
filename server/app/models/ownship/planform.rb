class Ownship::Planform
  attr_reader :key, :outline, :hot_spot, :sources

  def initialize(key:, name:, outline:, hot_spot:, sources: [])
    @key = key
    @name = name
    @outline = outline.map { |x, y| [ x.to_f, y.to_f ] }.freeze
    @hot_spot = hot_spot.to_f
    @sources = sources
  end

  def name
    @name.is_a?(Hash) ? @name.fetch(I18n.locale.to_s) { @name.fetch(I18n.default_locale.to_s) } : @name
  end

  def span = 2 * outline.map(&:first).max
  def length = outline.map(&:last).max

  def to_h = { key:, name:, outline:, hotSpot: hot_spot }
end
