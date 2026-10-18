module Geocoder
  class Geoip2DatabaseManager
    def ensure_current(disk_epoch)
      if lookup_build_epoch >= disk_epoch
        Rails.logger.info "[Geocoder] Lookup is up to date (#{format_epoch(disk_epoch)})"
      else
        reload(disk_epoch)
      end
    end

    private
      # INFO: fc 17oct26 Geocoder caches the lookup in Geocoder::Lookup's @services, with no public API to reload it
      def reload(expected)
        cached = lookup_build_epoch

        services = Geocoder::Lookup.instance_variable_get(:@services)
        services&.delete(:geoip2)

        current = lookup_build_epoch

        if current >= expected
          Rails.logger.info "[Geocoder] Lookup reloaded (#{format_epoch(cached)} -> #{format_epoch(current)})"
        else
          Rails.logger.error "[Geocoder] Lookup still outdated after reload (#{format_epoch(current)}, expected >= #{format_epoch(expected)})"
        end
      end

      def lookup_build_epoch
        lookup_metadata.fetch("build_epoch")
      end

      def format_epoch(epoch)
        Time.at(epoch).utc
      end

      def lookup_metadata
        lookup = Geocoder::Lookup.get(:geoip2)
        lookup.instance_variable_get(:@mmdb).metadata
      end
  end
end
