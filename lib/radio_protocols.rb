class RadioProtocols
  NAMES = { "adsl" => "ADS-L", "adsb" => "ADS-B", "alptas" => "ALP-TAS" }.freeze
  VERSION = /\A\d+\.\d+\.\d+\z/
  DECLARATION = /^inline constexpr char kVersion\[\] = "([^"]*)";$/

  def initialize(firmware)
    @firmware = firmware
  end

  def of(product)
    stems(product).map { |stem| "#{NAMES.fetch(stem)} #{version(stem)}" }.join(", ")
  end

  def notes(version:, products:)
    [
      "# Radio protocols",
      "",
      "skyBlip #{version} carries these radio protocol versions.",
      "",
      *products.map { |product| "- `#{product.tr("_", "-")}`: #{of(product)}" }
    ].join("\n") + "\n"
  end

  private
    def stems(product)
      file = File.join(@firmware, "products", product, "PROTOCOLS")
      raise ArgumentError, "No PROTOCOLS file for #{product} in #{File.dirname(file, 2)}" unless File.file?(file)

      stems = File.readlines(file, chomp: true).map(&:strip).reject(&:empty?)
      unknown = stems - NAMES.keys
      raise ArgumentError, "Unknown radio protocol #{unknown.join(", ")} in #{file}" if unknown.any?
      raise ArgumentError, "No radio protocol in #{file}" if stems.empty?

      stems
    end

    def version(stem)
      header = "protocols/#{stem}/version.h"
      declarations = File.read(File.join(@firmware, header)).scan(DECLARATION).flatten
      raise ArgumentError, "No single kVersion in #{header}" unless declarations.one?

      version = declarations.first
      if VERSION.match?(version)
        version
      else
        raise ArgumentError, "Invalid #{NAMES.fetch(stem)} version in #{header}: #{version.inspect}"
      end
    end
end
