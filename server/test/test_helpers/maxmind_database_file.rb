require "maxminddb"

class MaxmindDatabaseFile
  METADATA_MARKER = MaxMindDB::Client::METADATA_BEGIN_MARKER
  SEARCH_TREE_SEPARATOR = ("\0" * MaxMindDB::Client::DATA_SECTION_SEPARATOR_SIZE).b.freeze

  UTF8 = 2
  UINT32 = 6
  MAP = 7
  ARRAY = 11

  def self.write(path, build_epoch:, place: nil)
    File.binwrite(path, bytes(build_epoch:, place:))
  end

  def self.bytes(build_epoch:, place: nil)
    new(build_epoch, place).bytes
  end

  def initialize(build_epoch, place = nil)
    @build_epoch, @place = build_epoch, place
  end

  def bytes
    search_tree + SEARCH_TREE_SEPARATOR + data_section + METADATA_MARKER + encode(metadata)
  end

  private
    def search_tree
      if @place
        record(node_count) + record(node_count + SEARCH_TREE_SEPARATOR.bytesize)
      else
        "".b
      end
    end

    def data_section = @place ? encode(@place) : "".b

    def node_count = @place ? 1 : 0

    def record(value) = [ value ].pack("N")[1..]

    def metadata
      {
        "node_count" => node_count,
        "record_size" => 24,
        "ip_version" => @place ? 4 : 6,
        "database_type" => "GeoLite2-City",
        "languages" => [ "en" ],
        "binary_format_major_version" => 2,
        "binary_format_minor_version" => 0,
        "build_epoch" => @build_epoch,
        "description" => { "en" => "skyBlip test database" }
      }
    end

    def encode(value)
      case value
      when Hash    then control(MAP, value.size) + value.flat_map { |k, v| [ encode(k), encode(v) ] }.join
      when Array   then control(ARRAY, value.size) + value.map { encode(it) }.join
      when String  then control(UTF8, value.bytesize) + value.b
      when Integer then encode_uint32(value)
      else raise ArgumentError, "#{value.class} has no MaxMind DB encoding here"
      end
    end

    def encode_uint32(value)
      bytes = [ value ].pack("N").sub(/\A\0+/, "")
      control(UINT32, bytes.bytesize) + bytes
    end

    def control(type, size)
      raise ArgumentError, "size #{size} needs the extended encoding" if size >= 29

      if type > MAP
        [ size, type - MAP ].pack("C2")
      else
        [ (type << 5) | size ].pack("C")
      end
    end
end
