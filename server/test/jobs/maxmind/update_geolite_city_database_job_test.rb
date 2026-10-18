require "test_helper"
require_relative "../../test_helpers/geoip2_database_helper"
require_relative "../../test_helpers/log_capture_helper"

class Maxmind::UpdateGeoliteCityDatabaseJobTest < ActiveJob::TestCase
  include Geoip2DatabaseHelper, LogCaptureHelper

  test "performing the job keeps a current database" do
    output = capture_log do
      VCR.use_cassette("maxmind/geolite_city_last_modified") do
        Maxmind::UpdateGeoliteCityDatabaseJob.perform_now
      end
    end

    assert_match(/\[MaxMind\] Database is up to date/, output)
  end
end
