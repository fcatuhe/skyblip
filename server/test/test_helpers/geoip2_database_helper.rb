require_relative "maxmind_database_file"

module Geoip2DatabaseHelper
  extend ActiveSupport::Concern

  BUILD_EPOCH = Time.utc(2026, 3, 3).to_i
  MAXMIND_PUBLISHED_AT = Time.utc(2026, 9, 8, 20, 0, 54)

  included do
    setup do
      @geoip2_database_path = Pathname.new(Dir.mktmpdir).join("GeoLite2-City.mmdb")
      @configured_geoip2_file = Rails.configuration.x.geolite_city_database

      write_geoip2_database(BUILD_EPOCH)
      Rails.configuration.x.geolite_city_database = @geoip2_database_path
    end

    teardown do
      Rails.configuration.x.geolite_city_database = @configured_geoip2_file
      FileUtils.remove_entry(@geoip2_database_path.dirname)
    end
  end

  private
    def write_geoip2_database(build_epoch)
      MaxmindDatabaseFile.write(@geoip2_database_path, build_epoch:)
      FileUtils.touch(@geoip2_database_path, mtime: MAXMIND_PUBLISHED_AT)
    end
end
