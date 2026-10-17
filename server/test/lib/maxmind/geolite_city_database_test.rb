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
end
