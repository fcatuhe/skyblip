require "minitest/autorun"
require "digest"
require "fileutils"
require "json"
require "open3"
require "rbconfig"
require "tmpdir"
require_relative "../../lib/dev_release"

class DevReleaseTest < Minitest::Test
  ROOT = File.expand_path("../..", __dir__)
  GO_NOTES = <<~NOTES.chomp
    skyBlip 0.1.0+1010 from main, built from deadbeef and signed with the development key.

    ## skyBlip Go

    Radio protocols: ADS-L 0.1.0, ALP-TAS 0.1.0.

    - `.signed.bin` installs over Bluetooth only on a unit whose bootloader trusts that key ([firmware/keys](https://github.com/fcatuhe/skyblip/tree/main/firmware/keys)), older builds included.
    - `.full.signed.bin` is the same image with the sensor hub's firmware, for a unit the update page asks it for.
    - `.uf2` installs on any unit by drag-and-drop, and moves it to the development key.
  NOTES
  BRAVO_NOTES = <<~NOTES.chomp
    ## skyBlip Bravo

    Radio protocols: ADS-B 0.1.0.

    - `.signed.bin` installs over Bluetooth only on a unit whose bootloader trusts that key ([firmware/keys](https://github.com/fcatuhe/skyblip/tree/main/firmware/keys)), older builds included.
    - `.uf2` installs by drag-and-drop on a MeshTracker X1 only, after backing up its factory flash as [#169](https://github.com/fcatuhe/skyblip/issues/169) describes. No skyBlip Bravo image has run on an X1 yet.
  NOTES
  SHARED_FILES = %w[Apache-2.0.txt LICENSE NOTICE.md SHA256SUMS TinyCrypt.txt embed.js scenarios.json skyblip_simulator.js skyblip_simulator.wasm].freeze
  GO_FILES = %w[skyblip-go-%s.full.signed.bin skyblip-go-%s.signed.bin skyblip-go-%s.uf2].freeze
  BRAVO_FILES = %w[skyblip-bravo-%s.signed.bin skyblip-bravo-%s.uf2].freeze

  # Answers like gh: a created tag appears in the next list, an uploaded file is kept under uploads/<tag>/.
  FAKE_GH = <<~'RUBY'.freeze
    #!/usr/bin/env ruby
    require "json"
    require "fileutils"
    state = ENV.fetch("FAKE_GH_STATE")
    tags = File.join(state, "tags")
    File.open(File.join(state, "calls"), "a") { |log| log.puts JSON.generate(ARGV.map { |arg| File.file?(arg) ? File.basename(arg) : arg }) }
    case ARGV.first(2)
    in [ "release", "view" ] then exit(File.readlines(tags, chomp: true).include?(ARGV[2]) ? 0 : 1)
    in [ "release", "list" ] then puts File.read(tags)
    in [ "pr", "list" ] then puts File.read(File.join(state, "branches"))
    in [ "release", "create" ]
      uploads = File.join(state, "uploads", ARGV[2])
      FileUtils.mkdir_p(uploads)
      FileUtils.cp(ARGV.select { |arg| File.file?(arg) }, uploads)
      File.open(tags, "a") { |list| list.puts ARGV[2] }
    in [ "release", "delete" ] then nil
    end
  RUBY

  def test_main_publishes_every_product_in_one_release_and_prunes_the_fourth_newest
    Dir.mktmpdir do |root|
      run = publish(root, ref: "main", products: %w[skyblip-go skyblip-bravo], tags: %w[dev-998 dev-1002 dev-1005 v0.1.0])

      assert run.success, run.output
      files = (SHARED_FILES + (GO_FILES + BRAVO_FILES).map { |file| file % "dev-1010" }).sort
      assert_equal [ [ "release", "create", "dev-1010", *files, "--target", "deadbeef", "--prerelease", "--latest=false",
                       "--title", "skyBlip dev 0.1.0+1010", "--notes", "#{GO_NOTES}\n\n#{BRAVO_NOTES}",
                       "--generate-notes", "--notes-start-tag", "dev-1005" ] ], run.creates
      assert_equal [ %w[release delete dev-998 --cleanup-tag --yes] ], run.deletes
      assert_checksums File.join(root, "gh/uploads/dev-1010")
    end
  end

  def test_a_run_of_one_product_publishes_that_product_alone
    Dir.mktmpdir do |root|
      run = publish(root, ref: "main", products: %w[skyblip-go], tags: [])

      assert run.success, run.output
      create = run.creates.first
      assert_equal (SHARED_FILES + GO_FILES.map { |file| file % "dev-1010" }).sort, create[3...create.index("--target")]
      assert_equal GO_NOTES, create[create.index("--notes") + 1]
    end
  end

  def test_a_branch_keeps_its_newest_build_while_its_pull_request_is_open
    Dir.mktmpdir do |root|
      run = publish(root, ref: "feat/x", products: %w[skyblip-go], tags: %w[dev-1005 dev-1001-feat-x dev-1003-feat-x dev-1000-closed],
                          branches: %w[feat/x other])

      assert run.success, run.output
      create = run.creates.first
      assert_equal [ "dev-1010-feat-x", "skyBlip dev 0.1.0+1010 (feat/x)" ], [ create[2], create[create.index("--title") + 1] ]
      refute_includes create, "--notes-start-tag"
      assert_equal %w[dev-1000-closed dev-1001-feat-x dev-1003-feat-x], run.deletes.map { |call| call[2] }.sort
    end
  end

  # A rerun of an older commit keeps the build it published as well as the branch's newest.
  def test_a_rerun_publishes_nothing_and_keeps_the_build_it_published
    Dir.mktmpdir do |root|
      run = publish(root, ref: "feat-x", products: %w[skyblip-go], tags: %w[dev-1009-feat-x dev-1010-feat-x dev-1011-feat-x], branches: %w[feat-x])

      assert run.success, run.output
      assert_empty run.creates
      assert_equal [ %w[release delete dev-1009-feat-x --cleanup-tag --yes] ], run.deletes
    end
  end

  # tr worked on bytes, so a two-byte character becomes two dashes.
  def test_a_branch_label_keeps_letters_digits_and_dots_byte_for_byte
    assert_equal "fix-caf---v1.2", DevRelease.branch_label("fix/café v1.2")
  end

  # Each product had its own pre-releases before one release carried them all, and their owner deletes them by hand.
  def test_the_pre_releases_of_one_product_are_left_alone
    Dir.mktmpdir do |root|
      run = publish(root, ref: "main", products: %w[skyblip-go skyblip-bravo], tags: %w[dev-1002 dev-1005 bravo-dev-998 bravo-dev-1001 bravo-dev-1005])

      assert run.success, run.output
      create = run.creates.first
      assert_equal "dev-1005", create[create.index("--notes-start-tag") + 1]
      assert_empty run.deletes
    end
  end

  def test_products_that_disagree_on_the_version_publish_nothing
    Dir.mktmpdir do |root|
      run = publish(root, ref: "main", products: %w[skyblip-go skyblip-bravo], tags: []) do |images, _source|
        File.write(File.join(images, "skyblip-bravo.version"), "skyblip-bravo 0.2.0+1010 abc1234 development\n")
      end

      refute run.success
      assert_empty run.creates
      assert_includes run.output, "The products disagree on the version: skyblip-bravo 0.2.0+1010, skyblip-go 0.1.0+1010"
    end
  end

  def test_a_product_missing_a_file_publishes_nothing
    Dir.mktmpdir do |root|
      run = publish(root, ref: "main", products: %w[skyblip-go skyblip-bravo], tags: []) do |images, _source|
        File.delete(File.join(images, "skyblip-bravo.uf2"))
      end

      refute run.success
      assert_empty run.creates
      assert_includes run.output, "skyblip-bravo.uf2"
    end
  end

  def test_a_product_with_no_radio_protocols_listed_publishes_nothing
    Dir.mktmpdir do |root|
      run = publish(root, ref: "main", products: %w[skyblip-go skyblip-tango], tags: [])

      refute run.success
      assert_empty run.creates
      assert_includes run.output, "No radio protocols listed for skyblip-tango"
    end
  end

  def test_a_missing_license_text_fails_the_run
    Dir.mktmpdir do |root|
      run = publish(root, ref: "main", products: %w[skyblip-go], tags: []) do |_images, source|
        Dir.glob(File.join(source, "LICENSES", "*")).each { |file| File.delete(file) }
      end

      refute run.success
      assert_empty run.creates
      assert_includes run.output, "No license texts"
    end
  end

  def test_a_protocol_version_that_is_not_a_triple_fails_the_run
    Dir.mktmpdir do |root|
      run = publish(root, ref: "main", products: %w[skyblip-go], tags: []) do |_images, source|
        File.write(File.join(source, "firmware/core/protocol/adsl.version"), "1.0\n")
      end

      refute run.success
      assert_empty run.creates
      assert_includes run.output, "Invalid ADS-L version in adsl.version"
    end
  end

  def test_a_run_with_no_product_image_fails
    Dir.mktmpdir do |root|
      run = publish(root, ref: "main", products: [], tags: [])

      refute run.success
      assert_includes run.output, "No product image"
    end
  end

  private
    Run = Struct.new(:success, :output, :calls) do
      def creates = calls.select { |call| call.first(2) == %w[release create] }
      def deletes = calls.select { |call| call.first(2) == %w[release delete] }
    end

    def publish(root, ref:, products:, tags:, branches: [])
      images, simulator, source, state = %w[image simulator source gh].map { |name| File.join(root, name).tap { |dir| FileUtils.mkdir_p(dir) } }
      products.each { |slug| product_image(images, slug, hub_image: slug == "skyblip-go") }
      DevRelease::SIMULATOR_FILES.each { |file| File.write(File.join(simulator, file), file) }
      FileUtils.mkdir_p(File.join(source, "LICENSES"))
      %w[LICENSE NOTICE.md LICENSES/Apache-2.0.txt LICENSES/TinyCrypt.txt].each { |file| File.write(File.join(source, file), file) }
      FileUtils.mkdir_p(File.join(source, "firmware/core/protocol"))
      %w[adsl adsb alptas].each { |protocol| File.write(File.join(source, "firmware/core/protocol/#{protocol}.version"), "0.1.0\n") }
      yield images, source if block_given?
      File.write(File.join(state, "tags"), tags.map { |tag| "#{tag}\n" }.join)
      File.write(File.join(state, "branches"), branches.map { |branch| "#{branch}\n" }.join)
      File.write(File.join(state, "calls"), "")
      File.write(File.join(state, "gh"), FAKE_GH)
      File.chmod(0o755, File.join(state, "gh"))

      env = { "PATH" => "#{state}:#{ENV.fetch("PATH")}", "HOME" => ENV.fetch("HOME"), "FAKE_GH_STATE" => state, "GITHUB_REF_NAME" => ref, "GITHUB_SHA" => "deadbeef", "GH_REPO" => "fcatuhe/skyblip" }
      output, status = Open3.capture2e(env, RbConfig.ruby, File.join(ROOT, "scripts/dev_release.rb"), images, simulator, source,
                                    unsetenv_others: true)
      Run.new(status.success?, output, File.readlines(File.join(state, "calls")).map { |line| JSON.parse(line) })
    end

    def product_image(images, slug, hub_image:)
      File.write(File.join(images, "#{slug}.uf2"), "#{slug} uf2\n")
      File.write(File.join(images, "#{slug}.signed.bin"), "#{slug} slim image\n")
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
