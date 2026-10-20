class Ownship::Widget
  PATH = Rails.root.join("../firmware/ui/widgets/skyship.cpp")
  TABLE = /constexpr int kGlyphWidth = \d+;\s*\nconstexpr int kGlyphOriginRow = \d+;\s*\nconstexpr uint32_t kOwnshipRows\[\] = \{.*?\n\};/m

  Unfit = Class.new(StandardError)

  attr_reader :path

  def initialize(path = PATH)
    @path = Pathname(path)
  end

  def ownship = Ownship.parse(path.read[TABLE])

  def compile(ownship)
    if compilable?(ownship)
      source = path.read
      written = source.sub(TABLE) { ownship.to_cpp }
      path.write(written) if written != source
      written != source
    else
      raise Unfit, "the compiled symbol is #{Ownship::SPAN} by #{Ownship::ROWS}, hot spot on row #{Ownship::ROWS_TO_NOSE}, and breaks no check: " \
        "this one is #{ownship.width} by #{ownship.height}, hot spot on row #{ownship.hot_spot}, problems: #{ownship.problems.map(&:key).join(", ").presence || "none"}"
    end
  end

  private
    # INFO: fc 18oct26 skyship.cpp static_asserts these exact sizes, the screens measure off them
    def compilable?(ownship)
      ownship.fits_device? && [ ownship.width, ownship.height, ownship.hot_spot ] == [ Ownship::SPAN, Ownship::ROWS, Ownship::ROWS_TO_NOSE ]
    end
end
