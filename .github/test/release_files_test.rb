require "minitest/autorun"
require "digest"
require "fileutils"
require "tmpdir"
require_relative "../../lib/release_files"

class ReleaseFilesTest < Minitest::Test
  ROOT = File.expand_path("../..", __dir__)

  def test_a_release_names_each_products_images_after_it_and_adds_the_simulator_the_notices_and_the_checksums
    within do |images, simulator, source, release|
      ReleaseFiles.new(images).stage(into: release, name: "dev-1010", simulator:, source:)

      assert_equal %w[NOTICE.md SHA256SUMS embed.js scenarios.json skyblip-bravo-dev-1010.signed.bin skyblip-bravo-dev-1010.uf2
                      skyblip-go-dev-1010.full.signed.bin skyblip-go-dev-1010.signed.bin skyblip-go-dev-1010.uf2
                      skyblip_simulator.js skyblip_simulator.wasm], Dir.children(release).sort
      assert_equal "skyblip-go base image\n", File.read(File.join(release, "skyblip-go-dev-1010.signed.bin"))
      assert_checksums release
    end
  end

  def test_the_notices_and_every_license_text_ship_as_one_file
    within do |images, simulator, source, release|
      ReleaseFiles.new(images).stage(into: release, name: "0.2.0", simulator:, source:)

      assert_equal <<~NOTICE, File.read(File.join(release, "NOTICE.md"))
        NOTICE.md

        ## License texts

        ### LICENSE

        ```
        LICENSE
        ```

        ### LICENSES/Apache-2.0.txt

        ```
        LICENSES/Apache-2.0.txt
        ```
      NOTICE
    end
  end

  def test_a_production_release_lists_each_products_radio_protocols
    within do |images, simulator, source, release|
      ReleaseFiles.new(images, products: %w[skyblip_go skyblip_bravo]).stage(into: release, name: "0.2.0", simulator:, source:, protocols: true)

      versions = %w[adsl adsb alptas].to_h { |stem| [ stem, File.read(File.join(ROOT, "firmware/protocols/#{stem}/version.h"))[RadioProtocols::DECLARATION, 1] ] }
      notes = File.read(File.join(release, "PROTOCOLS.md"))
      assert_includes notes, "skyBlip 0.2.0 carries these radio protocol versions"
      assert_includes notes, "- `skyblip-bravo`: ADS-B #{versions["adsb"]} (1090 MHz extended squitter DF17, read only, after RTCA DO-260B (draft 4.2, 1090-WP30-18))\n"
      assert_includes notes, "- `skyblip-go`: ADS-L #{versions["adsl"]} (ADS-L 4 SRD-860 Issue 2), " \
                             "ALP-TAS #{versions["alptas"]} (ALP-TAS 2024 frame, version 3, skyBlip's reading of public sources)\n"
      assert_includes File.read(File.join(release, "SHA256SUMS")), "  PROTOCOLS.md\n"
    end
  end

  def test_a_production_release_refuses_images_of_other_products_than_it_asks_for
    within do |images, _simulator, _source, _release|
      image(images, "skyblip-tango", hub_image: false)
      error = assert_raises(ArgumentError) { ReleaseFiles.new(images, products: %w[skyblip_go skyblip_bravo]) }

      assert_equal "The images are of skyblip-bravo, skyblip-go, skyblip-tango, where the products ask for skyblip-bravo, skyblip-go", error.message
    end
  end

  def test_products_that_disagree_on_the_version_are_refused
    within do |images, _simulator, _source, _release|
      File.write(File.join(images, "skyblip-bravo.version"), "skyblip-bravo 0.2.0+1010 abc1234 development\n")
      error = assert_raises(ArgumentError) { ReleaseFiles.new(images) }

      assert_equal "The products disagree on the version: skyblip-bravo 0.2.0+1010, skyblip-go 0.1.0+1010", error.message
    end
  end

  def test_a_product_missing_an_image_is_refused
    within do |images, _simulator, _source, _release|
      File.delete(File.join(images, "skyblip-bravo.uf2"))
      error = assert_raises(ArgumentError) { ReleaseFiles.new(images) }

      assert_equal "Missing from #{images}: skyblip-bravo.uf2", error.message
    end
  end

  # Every image artifact lands in one directory, so a file no product build writes is refused rather than ignored.
  def test_a_file_no_product_build_writes_is_refused
    %w[skyblip-go.signed.bin.orig embed.js].each do |planted|
      within do |images, _simulator, _source, _release|
        File.write(File.join(images, planted), "planted\n")
        error = assert_raises(ArgumentError) { ReleaseFiles.new(images) }

        assert_equal "Unexpected files in #{images}: #{planted}", error.message
      end
    end
  end

  def test_a_directory_with_no_product_image_is_refused
    Dir.mktmpdir do |images|
      error = assert_raises(ArgumentError) { ReleaseFiles.new(images) }

      assert_equal "No product image in #{images}", error.message
    end
  end

  def test_a_missing_license_text_stages_nothing_further
    within do |images, simulator, source, release|
      Dir.glob(File.join(source, "LICENSES", "*")).each { |file| File.delete(file) }
      error = assert_raises(ArgumentError) { ReleaseFiles.new(images).stage(into: release, name: "0.2.0", simulator:, source:) }

      assert_equal "No license texts in #{source}/LICENSES", error.message
      refute File.exist?(File.join(release, "SHA256SUMS"))
    end
  end

  private
    def within
      Dir.mktmpdir do |root|
        images, simulator, source = %w[images simulator source].map { |name| File.join(root, name).tap { |dir| FileUtils.mkdir_p(dir) } }
        image(images, "skyblip-go", hub_image: true)
        image(images, "skyblip-bravo", hub_image: false)
        ReleaseFiles::SIMULATOR_FILES.each { |file| File.write(File.join(simulator, file), file) }
        FileUtils.mkdir_p(File.join(source, "LICENSES"))
        %w[LICENSE NOTICE.md LICENSES/Apache-2.0.txt].each { |file| File.write(File.join(source, file), file) }
        %w[protocols/adsl/version.h protocols/adsb/version.h protocols/alptas/version.h products/skyblip_go/PROTOCOLS products/skyblip_bravo/PROTOCOLS].each do |file|
          FileUtils.mkdir_p(File.join(source, "firmware", File.dirname(file)))
          FileUtils.cp(File.join(ROOT, "firmware", file), File.join(source, "firmware", file))
        end
        yield images, simulator, source, File.join(root, "release")
      end
    end

    def image(images, slug, hub_image:)
      File.write(File.join(images, "#{slug}.uf2"), "#{slug} uf2\n")
      File.write(File.join(images, "#{slug}.signed.bin"), "#{slug} base image\n")
      File.write(File.join(images, "#{slug}.full.signed.bin"), "#{slug} full image\n") if hub_image
      File.write(File.join(images, "#{slug}.signed.confirmed.bin"), "#{slug} confirmed image\n")
      File.write(File.join(images, "#{slug}.version"), "#{slug} 0.1.0+1010 abc1234 development\n")
    end

    def assert_checksums(release)
      listed = File.read(File.join(release, "SHA256SUMS")).lines.to_h { |line| line.chomp.split("  ", 2).reverse }

      assert_equal Dir.children(release).sort - %w[SHA256SUMS], listed.keys
      listed.each { |file, digest| assert_equal Digest::SHA256.file(File.join(release, file)).hexdigest, digest, file }
    end
end
