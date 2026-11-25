require "rubygems/package"
require_relative "maxmind_database_file"

# INFO: fc 25nov26 the GeoLite licence forbids publishing the database, so a recording keeps MaxMind's answers around a stand-in archive
module RecordedGeoliteArchive
  BUILD_EPOCH = Time.utc(2026, 11, 24).to_i

  def self.tar_gz
    @tar_gz ||= begin
      tar = StringIO.new
      Gem::Package::TarWriter.new(tar) do |writer|
        mmdb = MaxmindDatabaseFile.bytes(build_epoch: BUILD_EPOCH)
        writer.add_file_simple("GeoLite2-City/GeoLite2-City.mmdb", 0o644, mmdb.bytesize) { it.write(mmdb) }
      end
      gzip = StringIO.new
      Zlib::GzipWriter.wrap(gzip) { it.mtime = 1; it.write(tar.string) }
      gzip.string.b
    end
  end

  def self.sha256 = "#{Digest::SHA256.hexdigest(tar_gz)}  GeoLite2-City.tar.gz\n"
end
