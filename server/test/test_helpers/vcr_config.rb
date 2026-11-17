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

  config.filter_sensitive_data("<MAXMIND_LICENSE_KEY>") do |interaction|
    interaction.request.headers["Authorization"]&.first if interaction.request.uri.include?("maxmind.com")
  end
end
