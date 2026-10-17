require "net/http"

class Release
  Unavailable = Class.new(StandardError)
  NotFound = Class.new(StandardError)
  REPOSITORY = "fcatuhe/skyblip".freeze
  API_URL = "https://api.github.com/repos/#{REPOSITORY}/releases?per_page=100".freeze
  TAG = /\A(?:v\d+\.\d+\.\d+(?:[.+-][A-Za-z0-9.-]+)?|dev-\d+(?:-[A-Za-z0-9.-]+)?)\z/
  FILE = /\A(?:skyblip-go-[A-Za-z0-9.+-]+(?:\.full)?\.signed\.bin|skyblip_simulator\.(?:js|wasm)|embed\.js|scenarios\.json)\z/
  SIMULATOR_FILES = %w[skyblip_simulator.js skyblip_simulator.wasm embed.js].freeze
  DOWNLOAD_HOSTS = %w[github.com release-assets.githubusercontent.com objects.githubusercontent.com].freeze
  MAX_BYTES = 32 * 1024 * 1024

  def self.all
    response = request(URI(API_URL))
    entries = JSON.parse(response.body)
    raise TypeError unless entries.is_a?(Array)

    releases = entries.reject { |entry| entry.fetch("draft") }
      .select { |entry| TAG.match?(entry.fetch("tag_name")) }
      .map { |entry| new(entry) }
      .sort_by(&:published_at).reverse
    [ releases.find(&:stable?), *releases.select(&:development?) ].compact
  rescue JSON::ParserError, KeyError, TypeError, ArgumentError => error
    raise Unavailable, "Invalid GitHub release list: #{error.class}"
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
  def firmware_files = assets.select { |name| FILE.match?(name) && name.end_with?(".signed.bin") }
end
