VCR.configure do |config|
  config.ignore_localhost = true
  config.allow_http_connections_when_no_cassette = false
  config.cassette_library_dir = "test/vcr_cassettes"
  config.hook_into :webmock
  config.default_cassette_options = {
    record: ENV["CASSETTES_RECORD"] == "1" ? :all : :once
  }

  # INFO: fc 17oct26 GitHub redirects a download to a signed URL that expires, so the recording keeps none of it.
  config.filter_sensitive_data("https://release-assets.githubusercontent.com/recorded-asset") do |interaction|
    location = interaction.response.headers["Location"]&.first
    if location&.start_with?("https://release-assets.githubusercontent.com/")
      location
    elsif interaction.request.uri.start_with?("https://release-assets.githubusercontent.com/")
      interaction.request.uri
    end
  end
end
