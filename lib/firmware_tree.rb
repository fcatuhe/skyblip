class FirmwareTree
  VERSION_FIELDS = %w[VERSION_MAJOR VERSION_MINOR PATCHLEVEL].freeze
  BOARD = /^skyblip_product_board\(/

  def initialize(firmware)
    @firmware = firmware
  end

  def version
    file = File.join(@firmware, "VERSION")
    fields = File.read(file).scan(/^(\w+) *= *(\d+)/).to_h
    triple = fields.values_at(*VERSION_FIELDS)
    raise ArgumentError, "No version triple in #{file}" if triple.any?(&:nil?)

    triple.join(".")
  end

  def products
    products = Dir.glob("products/*/CMakeLists.txt", base: @firmware).sort
      .select { |cmake| File.read(File.join(@firmware, cmake)).match?(BOARD) }
      .map { |cmake| File.basename(File.dirname(cmake)) }
    raise ArgumentError, "No directory in #{@firmware}/products declares a board" if products.empty?

    products
  end
end
