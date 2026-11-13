class RadioProtocols
  NAMES = { "adsl" => "ADS-L", "adsb" => "ADS-B", "alptas" => "ALP-TAS" }.freeze
  VERSION = /\A\d+\.\d+\.\d+\z/

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
      version = File.read(File.join(@firmware, "core/protocol/#{stem}.version")).strip
      if VERSION.match?(version)
        version
      else
        raise ArgumentError, "Invalid #{NAMES.fetch(stem)} version in #{stem}.version: #{version.inspect}"
      end
    end
end
