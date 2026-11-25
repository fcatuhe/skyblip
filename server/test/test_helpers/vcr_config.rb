require_relative "recorded_geolite_archive"

VCR.configure do |config|
  config.ignore_localhost = true
  config.allow_http_connections_when_no_cassette = false
  config.cassette_library_dir = "test/vcr_cassettes"
  config.hook_into :webmock
  config.default_cassette_options = {
    record: ENV["CASSETTES_RECORD"] == "1" ? :all : :once
  }

  # INFO: fc 17oct26 GitHub redirects a download to a signed URL that expires, so the recording keeps only the file's name | 15nov26 one per asset
  signed_assets = "https://release-assets.githubusercontent.com/"
  # INFO: fc 17nov26 a cassette that records new episodes runs this again over interactions it already rewrote
  signed = ->(url) { url&.start_with?(signed_assets) && !url.start_with?("#{signed_assets}recorded-asset/") }
  recorded_asset = ->(signed_url) do
    file = URI.decode_www_form(URI(signed_url).query.to_s).to_h.fetch("rscd")[/filename=([^;]+)/, 1]
    "#{signed_assets}recorded-asset/#{file}"
  end
  config.before_record do |interaction|
    location = interaction.response.headers["Location"]&.first
    if signed.(location)
      interaction.response.headers["Location"] = [ recorded_asset.(location) ]
    elsif signed.(interaction.request.uri)
      interaction.request.uri = recorded_asset.(interaction.request.uri)
    end
  end

  # INFO: fc 25nov26 MaxMind redirects a download to a presigned URL that expires, so the recording keeps only its path
  presigned = ->(url) { url&.include?("X-Amz-Signature=") }
  config.before_record do |interaction|
    location = interaction.response.headers["Location"]&.first
    interaction.response.headers["Location"] = [ location.split("?").first ] if presigned.(location)
    interaction.request.uri = interaction.request.uri.split("?").first if presigned.(interaction.request.uri)

    body = interaction.response.body
    stand_in = if interaction.response.headers["Content-Type"]&.first == "application/gzip" && body.present?
      RecordedGeoliteArchive.tar_gz
    elsif body.match?(/\A\h{64}\s+GeoLite2-City_\d+\.tar\.gz\s*\z/)
      RecordedGeoliteArchive.sha256
    end
    if stand_in
      interaction.response.body = stand_in
      interaction.response.headers["Content-Length"] = [ stand_in.bytesize.to_s ]
    end
  end

  config.filter_sensitive_data("<MAXMIND_LICENSE_KEY>") do |interaction|
    interaction.request.headers["Authorization"]&.first if interaction.request.uri.include?("maxmind.com")
  end
end
