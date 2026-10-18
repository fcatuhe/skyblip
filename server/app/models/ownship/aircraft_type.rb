class Ownship::AircraftType
  KEYS = %w[
    unknown light_aircraft heavy_aircraft helicopter glider balloon
    microlight paraglider skydiver vtol gyrocopter
  ].freeze
  DEFAULT = "light_aircraft".freeze
  DRAWINGS = Rails.root.join("content/ownships")

  attr_reader :key

  def self.all = KEYS.map { new(it) }

  def self.find(key)
    KEYS.include?(key) ? new(key) : default
  end

  def self.default = new(DEFAULT)

  def initialize(key)
    @key = key
  end

  def code = KEYS.index(key)
  def name = I18n.t(key, scope: "ownships.aircraft_types")

  def drawn? = drawing_path.exist?

  def ownship
    if drawn?
      Ownship.parse(drawing.fetch("art"))
    else
      self.class.default.ownship
    end
  end

  def planforms
    if drawn?
      drawing.fetch("planforms").map { Ownship::Planform.new(**it.symbolize_keys.slice(:key, :name, :outline, :hot_spot, :sources)) }
    else
      self.class.default.planforms
    end
  end

  def ==(other) = other.is_a?(self.class) && key == other.key

  private
    def drawing_path = DRAWINGS.join("#{key}.yml")

    def drawing
      @drawing ||= YAML.safe_load_file(drawing_path)
    end
end
