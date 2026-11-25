require "test_helper"
require_relative "../../test_helpers/geoip2_database_helper"
require_relative "../../test_helpers/log_capture_helper"

class Maxmind::GeoliteCityDatabaseTest < ActiveSupport::TestCase
  include Geoip2DatabaseHelper, LogCaptureHelper

  # The database on disk is stamped with the Last-Modified the cassette answers, as a download stamps it.
  test "a database as recent as the one MaxMind publishes is not downloaded again" do
    output = capture_log do
      VCR.use_cassette("maxmind/geolite_city_last_modified") { Maxmind::GeoliteCityDatabase.update_now }
    end

    assert_not_requested :get, %r{https?://}
    assert_match(/\[MaxMind\] Database is up to date/, output)
    assert_match(/\[Geocoder\] Lookup is up to date/, output)
  end

  test "a database older than the one MaxMind publishes is replaced by the download its checksum vouches for" do
    FileUtils.touch(@geoip2_database_path, mtime: MAXMIND_PUBLISHED_AT - 1.day)
    Geocoder::Lookup.get(:geoip2)

    output = capture_log do
      VCR.use_cassette("maxmind/geolite_city_download") { Maxmind::GeoliteCityDatabase.update_now }
    end

    assert_equal RecordedGeoliteArchive::BUILD_EPOCH, MaxMindDB.new(@geoip2_database_path.to_s).metadata.fetch("build_epoch")
    assert_operator File.mtime(@geoip2_database_path), :>, MAXMIND_PUBLISHED_AT - 1.day
    assert_match(/\[MaxMind\] Database updated/, output)
    assert_match(/\[Geocoder\] Lookup reloaded/, output)
  end

  test "the credentials go to MaxMind and never to the storage host it redirects a download to" do
    FileUtils.touch(@geoip2_database_path, mtime: MAXMIND_PUBLISHED_AT - 1.day)

    capture_log do
      VCR.use_cassette("maxmind/geolite_city_download") { Maxmind::GeoliteCityDatabase.update_now }
    end

    assert_requested(:head, %r{\Ahttps://download\.maxmind\.com/}, times: 1) { it.headers["Authorization"]&.start_with?("Basic ") }
    assert_requested(:get, %r{\Ahttps://download\.maxmind\.com/}, times: 2) { it.headers["Authorization"]&.start_with?("Basic ") }
    assert_requested(:get, %r{\.r2\.cloudflarestorage\.com/}, times: 2) { !it.headers.key?("Authorization") }
  end
end
