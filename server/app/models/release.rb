require "net/http"

class Release
  Unavailable = Class.new(StandardError)
  NotFound = Class.new(StandardError)
  REPOSITORY = "fcatuhe/skyblip".freeze
  API_URL = "https://api.github.com/repos/#{REPOSITORY}/releases?per_page=100".freeze
  TAG = /\A(?:v\d+\.\d+\.\d+(?:[.+-][A-Za-z0-9.-]+)?|dev-\d+(?:-[A-Za-z0-9.-]+)?)\z/
  FIRMWARE = /\A(skyblip-(?:go|bravo))-[A-Za-z0-9.+-]+(?:\.full)?\.signed\.bin\z/
  FULL_FIRMWARE = /\.full\.signed\.bin\z/
  FILE = /#{FIRMWARE}|\A(?:skyblip_simulator\.(?:js|wasm)|embed\.js|scenarios\.json)\z/
  SIMULATOR_FILES = %w[skyblip_simulator.js skyblip_simulator.wasm embed.js].freeze
  DOWNLOAD_HOSTS = %w[github.com release-assets.githubusercontent.com objects.githubusercontent.com].freeze
  MAX_BYTES = 32 * 1024 * 1024

  # INFO: fc 11nov26 every device page asks for the list, and GitHub grants 60 unauthenticated requests an hour to the server's address
  def self.all
    body = Rails.cache.fetch("github/releases", expires_in: 5.minutes) { request(URI(API_URL)).body }
    entries = JSON.parse(body)
    raise TypeError unless entries.is_a?(Array)

    shelf entries.reject { |entry| entry.fetch("draft") }
      .select { |entry| TAG.match?(entry.fetch("tag_name")) }
      .map { |entry| new(entry) }
  rescue JSON::ParserError, KeyError, TypeError, ArgumentError => error
    raise Unavailable, "Invalid GitHub release list: #{error.class}"
  end

  def self.product_of(file) = file[FIRMWARE, 1]&.tr("-", "_")
  def self.kind_of(file) = FULL_FIRMWARE.match?(file) ? "full" : "slim"

  def self.firmware_shelf(releases)
    images = releases.sort_by(&:published_at).reverse.flat_map { |release| release.firmware_files.map { |file| [ release, file ] } }
    slim, full = images.partition { |_release, file| kind_of(file) == "slim" }
    slim + full
  end

  def self.shelf(releases)
    newest_first = releases.sort_by(&:published_at).reverse
    [ newest_first.find(&:stable?), *newest_first.select(&:development?) ].compact
  end

  def self.simulator(releases = all)
    available = releases.select(&:simulator?).sort_by(&:published_at).reverse
    available.find(&:stable?) || available.find(&:main_development?)
  end

  def self.download(tag, file)
    raise NotFound unless TAG.match?(tag) && FILE.match?(file)

    uri = URI("https://github.com/#{REPOSITORY}/releases/download/#{tag}/#{file}")
    request(uri, downloads: true).body
  end

  def self.request(uri, downloads: false, redirects: 3)
    # INFO: fc 17oct26 only our repository and GitHub asset hosts may supply executable simulator code or firmware.
    allowed = downloads ? DOWNLOAD_HOSTS.include?(uri.host) : uri.host == "api.github.com"
    raise Unavailable, "Unexpected GitHub destination" unless uri.scheme == "https" && allowed && uri.userinfo.nil?

    response = Net::HTTP.start(uri.host, uri.port, use_ssl: true, open_timeout: 5, read_timeout: 20, write_timeout: 5) do |http|
      request = Net::HTTP::Get.new(uri)
      request["User-Agent"] = "skyblip-server"
      request["Accept"] = downloads ? "application/octet-stream" : "application/vnd.github+json"
      http.request(request) do |reply|
        body = +"".b
        reply.read_body do |chunk|
          body << chunk
          raise Unavailable, "GitHub response exceeds size limit" if body.bytesize > MAX_BYTES
        end
        reply.body = body
      end
    end

    case response
    when Net::HTTPSuccess
      response
    when Net::HTTPRedirection
      raise Unavailable, "Too many GitHub redirects" unless redirects.positive?

      location = response["location"]
      raise Unavailable, "GitHub redirect without a location" unless location

      request(URI.join(uri, location), downloads:, redirects: redirects - 1)
    when Net::HTTPNotFound
      if downloads
        raise NotFound
      else
        raise Unavailable, "GitHub returned HTTP #{response.code}"
      end
    else
      raise Unavailable, "GitHub returned HTTP #{response.code}"
    end
  rescue Timeout::Error, SocketError, IOError, SystemCallError, OpenSSL::SSL::SSLError, URI::InvalidURIError,
         Net::HTTPBadResponse, Net::ProtocolError => error
    raise Unavailable, "GitHub request failed: #{error.class}"
  end
  private_class_method :request

  attr_reader :tag, :name, :assets, :published_at

  def initialize(entry)
    @tag = entry.fetch("tag_name")
    @name = entry.fetch("name").presence || tag
    @assets = entry.fetch("assets").map { |asset| asset.fetch("name") }
    @prerelease = entry.fetch("prerelease")
    @published_at = Time.iso8601(entry.fetch("published_at"))
  end

  def development? = tag.start_with?("dev-")
  def main_development? = /\Adev-\d+\z/.match?(tag)
  def stable? = tag.start_with?("v") && !@prerelease
  def channel = development? ? "development" : "release"
  def url = "https://github.com/#{REPOSITORY}/releases/tag/#{tag}"
  def simulator? = (SIMULATOR_FILES - assets).empty?
  def firmware_files = assets.grep(FIRMWARE)
end
