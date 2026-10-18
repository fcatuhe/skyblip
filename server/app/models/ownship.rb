class Ownship
  include Checking

  Unreadable = Class.new(StandardError)

  MAX_WIDTH = 32
  MAX_HEIGHT = 32
  HOT_SPOT_MARK = "<"

  attr_reader :pixels

  def self.parse(text)
    words = text.scan(/(?<![0-9a-zA-Z])0x(\h{1,8})\s*,/).flatten.map(&:hex)
    lines = text.lines.select { it.match?(/[#.]{3,}/) }

    if words.size > 2
      width = text[/kGlyphW\s*=\s*(\d+)/, 1]&.to_i || MAX_WIDTH
      hot_spot = text[/kGlyphOriginRow\s*=\s*(\d+)/, 1]&.to_i
      from_words(words, width:, hot_spot:)
    elsif lines.size > 2
      art = lines.map { it[/[#.]{3,}/] }
      width = art.map(&:size).max
      new(art.map { |row| row.ljust(width, ".").chars.map { it == "#" ? 1 : 0 } }, hot_spot: lines.index { it.include?(HOT_SPOT_MARK) })
    else
      raise Unreadable, "no glyph: expected rows of '#' and '.' or a kOwnshipRows[] table"
    end
  end

  def self.from_param(rows, width:, hot_spot: nil)
    words = rows.to_s.scan(/\h{1,8}/).map(&:hex)
    width = Integer(width, exception: false)
    hot_spot = hot_spot.present? ? Integer(hot_spot, exception: false) : nil

    if width&.between?(3, MAX_WIDTH) && words.all? { it < 2**width }
      from_words(words, width:, hot_spot:)
    else
      raise Unreadable, "rows do not fit the width"
    end
  end

  def self.from_words(words, width:, hot_spot: nil)
    if width.between?(3, MAX_WIDTH) && words.size.between?(1, MAX_HEIGHT)
      new(words.map { |word| Array.new(width) { word[it] } }, hot_spot:)
    else
      raise Unreadable, "a glyph is 1 to #{MAX_HEIGHT} rows of 3 to #{MAX_WIDTH} columns"
    end
  end

  def initialize(pixels, hot_spot: nil)
    if pixels.empty? || pixels.size > MAX_HEIGHT || !pixels.first.size.between?(3, MAX_WIDTH) || pixels.map(&:size).uniq.size > 1
      raise Unreadable, "a glyph is 1 to #{MAX_HEIGHT} rows of 3 to #{MAX_WIDTH} equal columns"
    end

    @pixels = pixels.map { |row| row.map { it.to_i.clamp(0, 1) }.freeze }.freeze
    @hot_spot = hot_spot if hot_spot&.between?(0, height - 1)
  end

  def width = pixels.first.size
  def height = pixels.size
  def hot_spot = @hot_spot || widest_row

  def words
    pixels.map { |row| row.each_with_index.sum { |bit, column| bit << column } }
  end

  def hex_rows = words.map { format("%08x", it) }

  def to_h = { width:, words:, hotSpot: @hot_spot }

  def to_cpp
    table = pixels.zip(hex_rows).map { |row, hex| "    0x#{hex},  // #{row_art(row)}" }
    [
      "constexpr int kGlyphW = #{width};",
      "constexpr int kGlyphOriginRow = #{hot_spot};",
      "constexpr uint32_t kOwnshipRows[] = {",
      *table,
      "};"
    ].join("\n")
  end

  def ==(other)
    other.is_a?(Ownship) && pixels == other.pixels && hot_spot == other.hot_spot
  end

  private
    def widest_row
      inked = pixels.map(&:sum)
      inked.index(inked.max)
    end

    def row_art(row) = row.map { it == 1 ? "#" : "." }.join
end
