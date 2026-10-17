require "rubygems/package"

module Maxmind
  class Archive
    def initialize(data, checksum:)
      @data = data
      @checksum = checksum
    end

    def extract_to(path)
      verify_checksum
      Tempfile.create([ File.basename(path), ".download" ], File.dirname(path), binmode: true) do |download|
        download.write(mmdb)
        download.close
        File.rename(download.path, path)
      end
    end

    private
      def verify_checksum
        actual = Digest::SHA256.hexdigest(@data)
        unless actual == @checksum
          raise Maxmind::ChecksumError, "expected #{@checksum}, got #{actual}"
        end
      end

      def mmdb
        Gem::Package::TarReader.new(Zlib::GzipReader.new(StringIO.new(@data))) do |tar|
          tar.each do |entry|
            return entry.read if entry.full_name.end_with?(".mmdb")
          end
        end

        raise Maxmind::ExtractionError, "no .mmdb file found in archive"
      end
  end
end
