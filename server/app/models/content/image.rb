class Content::Image
  DIRECTORY = Rails.root.join("content_images")

  attr_reader :path

  def self.dimensions(path)
    @dimensions ||= Concurrent::Map.new
    @dimensions.compute_if_absent(path) { new(path).dimensions }
  end

  def initialize(path)
    @path = DIRECTORY.join(path)
  end

  def dimensions
    case path.extname
    when ".png" then path.binread(8, 16).unpack("NN")
    when ".avif" then avif_dimensions
    else raise ArgumentError, "No dimensions for #{path.extname} images: #{path}"
    end
  end

  private
    def avif_dimensions
      bytes = path.binread
      spatial_extent = bytes.index("ispe") || raise(ArgumentError, "No ispe box in #{path}")
      bytes.byteslice(spatial_extent + 8, 8).unpack("NN")
    end
end
