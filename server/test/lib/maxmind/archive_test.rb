require "test_helper"
require "rubygems/package"
require_relative "../../test_helpers/maxmind_database_file"

class Maxmind::ArchiveTest < ActiveSupport::TestCase
  BUILD_EPOCH = Time.utc(2026, 3, 3).to_i

  setup do
    @destination = Pathname.new(Dir.mktmpdir).join("GeoLite2-City.mmdb")
  end

  teardown do
    FileUtils.remove_entry(@destination.dirname)
  end

  test "the mmdb an archive carries is written to the destination" do
    database = MaxmindDatabaseFile.bytes(build_epoch: BUILD_EPOCH)

    archive("GeoLite2-City_20260303/GeoLite2-City.mmdb" => database).extract_to(@destination)

    assert_equal BUILD_EPOCH, MaxMindDB.new(@destination.to_s).metadata.fetch("build_epoch")
  end

  test "data that does not hash to the published checksum is refused" do
    corrupted = Maxmind::Archive.new("corrupted data", checksum: Digest::SHA256.hexdigest("the real archive"))

    assert_raises(Maxmind::ChecksumError) { corrupted.extract_to(@destination) }
    assert_not @destination.exist?
  end

  test "an archive without an mmdb in it is refused" do
    without_database = archive("GeoLite2-City_20260303/README.txt" => "no database here")

    assert_raises(Maxmind::ExtractionError) { without_database.extract_to(@destination) }
    assert_not @destination.exist?
  end

  private
    def archive(entries)
      tar_gz = gzip(tar(entries))
      Maxmind::Archive.new(tar_gz, checksum: Digest::SHA256.hexdigest(tar_gz))
    end

    def tar(entries)
      io = StringIO.new
      Gem::Package::TarWriter.new(io) do |tar|
        entries.each do |name, content|
          tar.add_file(name, 0o644) { it.write(content) }
        end
      end
      io.string
    end

    def gzip(data)
      io = StringIO.new
      gz = Zlib::GzipWriter.new(io)
      gz.write(data)
      gz.close
      io.string
    end
end
