module Maxmind
  class GeoliteCityDatabase
    BASE_URL = "https://download.maxmind.com/geoip/databases/GeoLite2-City/download".freeze

    def self.update_now
      new.update
    end

    def update
      if stale?
        download
        Rails.logger.info "[MaxMind] Database updated (#{build_date})"
      else
        Rails.logger.info "[MaxMind] Database is up to date (#{build_date})"
      end

      # INFO: fc 17oct26 always run: the geocoder caches its lookup in memory, and may serve a stale database
      Geocoder::Geoip2DatabaseManager.new.ensure_current(disk_build_epoch)
    end

    private
      def stale?
        !database_path.exist? || remote_last_modified > local_last_modified
      end

      def download
        tar_gz = fetch("#{BASE_URL}?suffix=tar.gz")
        checksum = fetch("#{BASE_URL}?suffix=tar.gz.sha256").split(/\s+/).first

        Maxmind::Archive.new(tar_gz, checksum:).extract_to(database_path)

        # INFO: fc 17oct26 the mtime carries the CDN's Last-Modified, so stale? compares without opening the database
        FileUtils.touch(database_path, mtime: remote_last_modified)
      end

      def build_date
        Time.at(disk_build_epoch).utc
      end

      def disk_build_epoch
        MaxMindDB.new(database_path.to_s).metadata.fetch("build_epoch")
      end

      def remote_last_modified
        @remote_last_modified ||= Time.parse(head("#{BASE_URL}?suffix=tar.gz")["last-modified"])
      end

      def local_last_modified
        File.mtime(database_path)
      end

      def fetch(url) = response_to(Net::HTTP::Get, url).body

      def head(url) = response_to(Net::HTTP::Head, url)

      def response_to(method, url, authenticated: true, redirects: 3)
        uri = URI(url)
        request = method.new(uri)
        # INFO: fc 25nov26 the licence key goes to MaxMind only, never to the storage host it redirects to
        request.basic_auth(account_id, license_key) if authenticated
        response = Net::HTTP.start(uri.host, uri.port, use_ssl: true) { it.request(request) }

        if response.is_a?(Net::HTTPRedirection) && redirects.positive?
          response_to(method, response["location"], authenticated: false, redirects: redirects - 1)
        else
          response.value
          response
        end
      end

      def database_path
        Pathname.new(Geocoder.config[:geoip2][:file])
      end

      def account_id = Rails.application.credentials.dig(:maxmind, :account_id)

      def license_key = Rails.application.credentials.dig(:maxmind, :license_key)
  end
end
