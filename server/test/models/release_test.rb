require "test_helper"
require_relative "../../../lib/firmware_tree"

class ReleaseTest < ActiveSupport::TestCase
  test "a simulator release must have all three runtime files, the scenarios aside" do
    assert_not release("dev-1005", assets: %w[skyblip-simulator-embed.js]).simulator?

    complete = release("dev-1005", assets: Release::SIMULATOR_RUNTIME)
    assert complete.simulator?
    assert complete.development?
  end

  # GitHub releases are immutable, so v0.8.0 keeps the names it was built with (#483).
  test "a release from before the rename holds no simulator" do
    old = release("v0.8.0", assets: %w[skyblip_simulator.js skyblip_simulator.wasm embed.js scenarios.json])

    assert_not old.simulator?
    assert_no_match Release::FILE, "skyblip_simulator.wasm"
    assert_match Release::FILE, "skyblip-simulator-scenarios.json"
  end

  test "firmware offerings exclude arbitrary release attachments" do
    stable = release("v0.1.0", name: nil, assets: %w[skyblip-go-0.1.0.signed.bin skyblip-go-0.1.0.full.signed.bin SHA256SUMS evil.js])

    assert_equal "v0.1.0", stable.name
    assert_equal "release", stable.channel
    assert_equal %w[skyblip-go-0.1.0.signed.bin skyblip-go-0.1.0.full.signed.bin], stable.firmware_files
  end

  # A unit installs the images whose key its bootloader names, so the shelf must name each image's key right.
  test "each channel names the KEYHASH prefix of the key in firmware/keys that signs it" do
    keys = { "development" => "development", "release" => "production" }.transform_values do |name|
      der = OpenSSL::PKey.read(Rails.root.join("../firmware/keys/#{name}.pub.pem").read).public_to_der
      OpenSSL::Digest::SHA256.hexdigest(der).first(8).upcase
    end

    assert_equal keys, Release::KEYS
    assert_equal "212602F2", release("dev-1005").key
    assert_equal "88A4BEF5", release("v0.1.0").key
  end

  # A release carries every product that declares a board, so the shelf must name each of them.
  test "every product the firmware declares is offered under its own name" do
    FirmwareTree.new(Rails.root.join("../firmware").to_s).products.each do |product|
      assert_equal product.delete_prefix("skyblip_"), Release.product_of("#{product.tr("_", "-")}-0.1.0.signed.bin"), product
    end
  end

  test "a release offers every product's images, each under the product its file names" do
    release = release("dev-1010", assets: %w[skyblip-go-dev-1010.signed.bin skyblip-go-dev-1010.full.signed.bin skyblip-bravo-dev-1010.signed.bin
                                             skyblip-tango-dev-1010.signed.bin embed.js])

    assert_equal %w[skyblip-go-dev-1010.signed.bin skyblip-go-dev-1010.full.signed.bin skyblip-bravo-dev-1010.signed.bin], release.firmware_files
    assert_equal %w[go go bravo], release.firmware_files.map { |file| Release.product_of(file) }
    assert_nil Release.product_of("embed.js")
  end

  test "a USB install takes the newest stable release holding the product's .uf2, else the newest main build, never a branch's" do
    main = release("dev-1005", prerelease: true, committed_at: "2026-10-14T00:00:00Z", assets: %w[skyblip-go-dev-1005.uf2 skyblip-bravo-dev-1005.uf2])
    branch = release("dev-1010-feat-x", prerelease: true, committed_at: "2026-10-15T00:00:00Z", assets: %w[skyblip-go-dev-1010-feat-x.uf2])
    stable = release("v0.1.0", committed_at: "2026-10-01T00:00:00Z", assets: %w[skyblip-go-0.1.0.uf2 skyblip-go-0.1.0.signed.bin])

    installers = Release.installers([ main, branch, stable ])

    assert_equal [ "v0.1.0", "skyblip-go-0.1.0.uf2" ], installers.fetch("go").then { |release, file| [ release.tag, file ] }
    assert_equal [ "dev-1005", "skyblip-bravo-dev-1005.uf2" ], installers.fetch("bravo").then { |release, file| [ release.tag, file ] }
    assert_empty Release.installers([ branch ])
  end

  # On Nov 19 two merges a few minutes apart could publish their builds in the other order, and the older one looked newest.
  test "a build is offered by when its commit was made, not by when its run published it" do
    newer = release("dev-2032", prerelease: true, committed_at: "2026-11-19T07:26:25Z", published_at: "2026-11-19T07:33:56Z", assets: %w[skyblip-go-dev-2032.uf2 skyblip-go-dev-2032.signed.bin] + Release::SIMULATOR_RUNTIME)
    older = release("dev-2030", prerelease: true, committed_at: "2026-11-19T07:23:13Z", published_at: "2026-11-19T07:36:10Z", assets: %w[skyblip-go-dev-2030.uf2 skyblip-go-dev-2030.signed.bin] + Release::SIMULATOR_RUNTIME)

    assert_equal %w[dev-2032 dev-2030], Release.shelf([ older, newer ]).map(&:tag)
    assert_equal "dev-2032", Release.installers([ older, newer ]).fetch("go").first.tag
    assert_equal "dev-2032", Release.simulator([ older, newer ]).tag
    assert_equal "skyblip-go-dev-2032.signed.bin", Release.firmware_shelf([ older, newer ]).first.last
  end

  # Each product had its own pre-releases, tagged bravo-dev-*, before one release carried them all.
  test "a tag naming a product is no release" do
    assert Release::TAG.match?("dev-1010-bravo-fix")
    assert_not Release::TAG.match?("bravo-dev-1010")
    assert_not Release::TAG.match?("bravo-v0.2.0")
    assert_raises(Release::NotFound) { Release.download("bravo-dev-1010", "skyblip-bravo-dev-1010.signed.bin") }
  end

  test "the shelf lists the published development builds newest first, a branch's included" do
    releases = VCR.use_cassette("github/releases") { Release.all }

    assert_equal %w[dev-1765 dev-1758 dev-1748 dev-1673-feat-bravo-adsb-trial], releases.map(&:tag)
  end

  test "the shelf keeps the newest stable release and every development build, never a release candidate" do
    older_stable = release("v0.1.0", committed_at: "2026-10-01T00:00:00Z")
    newer_stable = release("v0.2.0", committed_at: "2026-10-10T00:00:00Z")
    candidate = release("v0.3.0-rc1", prerelease: true, committed_at: "2026-10-12T00:00:00Z")
    main = release("dev-1005", prerelease: true, committed_at: "2026-10-14T00:00:00Z")
    branch = release("dev-1010-feat-x", prerelease: true, committed_at: "2026-10-15T00:00:00Z")

    assert_equal %w[v0.2.0 dev-1010-feat-x dev-1005], Release.shelf([ older_stable, main, candidate, branch, newer_stable ]).map(&:tag)
  end

  test "the firmware shelf lists every base image newest first, then every full one" do
    older = release("dev-1002", committed_at: "2026-10-14T00:00:00Z", assets: %w[skyblip-go-dev-1002.full.signed.bin skyblip-go-dev-1002.signed.bin])
    newer = release("dev-1005", committed_at: "2026-10-15T00:00:00Z", assets: %w[skyblip-go-dev-1005.full.signed.bin skyblip-go-dev-1005.signed.bin])

    assert_equal %w[skyblip-go-dev-1005.signed.bin skyblip-go-dev-1002.signed.bin skyblip-go-dev-1005.full.signed.bin skyblip-go-dev-1002.full.signed.bin],
                 Release.firmware_shelf([ older, newer ]).map(&:last)
  end

  test "the simulator prefers the newest stable release, then the newest main build" do
    older_stable = release("v0.1.0", committed_at: "2026-10-01T00:00:00Z", assets: Release::SIMULATOR_RUNTIME)
    newer_stable = release("v0.2.0", committed_at: "2026-10-10T00:00:00Z", assets: Release::SIMULATOR_RUNTIME)
    candidate = release("v0.3.0-rc1", prerelease: true, committed_at: "2026-10-12T00:00:00Z", assets: Release::SIMULATOR_RUNTIME)
    branch = release("dev-1010-feat-x", prerelease: true, committed_at: "2026-10-15T00:00:00Z", assets: Release::SIMULATOR_RUNTIME)
    main = release("dev-1005", prerelease: true, committed_at: "2026-10-14T00:00:00Z", assets: Release::SIMULATOR_RUNTIME)

    assert_equal "v0.2.0", Release.simulator([ branch, candidate, newer_stable, main, older_stable ]).tag
    assert_equal "dev-1005", Release.simulator([ branch, candidate, main ]).tag
    assert_nil Release.simulator([ branch ])
  end

  test "the prompt glass is drawn with the local build when one is complete" do
    Dir.mktmpdir do |directory|
      Release::SIMULATOR_RUNTIME.each { |file| FileUtils.touch(File.join(directory, file)) }

      with_config(simulator_build: directory) { assert_equal [ Release::LOCAL_TAG, "skyblip-simulator-embed.js" ], Release.prompt_glass_embed }
    end
  end

  test "with only the local build allowed, an incomplete one refuses the prompt glass rather than reach GitHub" do
    Dir.mktmpdir do |directory|
      FileUtils.touch(File.join(directory, "skyblip-simulator-embed.js"))

      with_config(simulator_build: directory, local_simulator_only: true) do
        assert_raises(Release::Unavailable) { Release.prompt_glass_embed }
      end
    end
  end

  test "without a local build, the prompt glass takes the newest stable release that holds the simulator" do
    with_config(simulator_build: nil, local_simulator_only: false) do
      assert_equal [ "v0.8.1", "skyblip-simulator-embed.js" ], VCR.use_cassette("github/simulator") { Release.prompt_glass_embed }
    end
  end

  test "the simulator is in no release when none holds its files" do
    assert_nil Release.simulator([ release("dev-1005", prerelease: true, assets: %w[skyblip-simulator-embed.js]) ])
  end

  test "the changelog holds a stable release's news and a main build's technical changes, newest first" do
    stable = release("v0.2.0", committed_at: "2026-10-10T00:00:00Z", body: "## What's new\n\n- The alarm warbles.\n\n## What's Changed\n* feat: warble by @fcatuhe in https://github.com/fcatuhe/skyblip/pull/130\n")
    main = release("dev-1005", name: "skyBlip dev 0.1.0+1005", prerelease: true, committed_at: "2026-10-14T00:00:00Z",
                               body: "## What's Changed\n* ui: draw traffic by @fcatuhe in https://github.com/fcatuhe/skyblip/pull/127\n")
    branch = release("dev-1010-feat-x", name: "skyBlip dev 0.1.0+1010 (feat-x)", prerelease: true, body: "## What's Changed\n* feat: x\n")
    silent = release("dev-1002", name: "skyBlip dev 0.1.0+1002", prerelease: true, body: nil)

    changelog = Release.changelog([ stable, branch, silent, main ], :en)

    assert_equal [ [ "0.1.0+1005", "development" ], [ "0.2.0", "release" ] ], changelog.map { |release, _| [ release.version, release.channel ] }
    assert_equal [ [ "ui: draw traffic" ], [ "The alarm warbles." ] ], changelog.map { |_, changes| changes.map(&:text) }
  end

  private
    def release(tag, name: "skyBlip", prerelease: false, committed_at: "2026-10-17T00:00:00Z", published_at: committed_at, assets: [], body: nil)
      Release.new("tag_name" => tag, "name" => name, "prerelease" => prerelease, "created_at" => committed_at, "published_at" => published_at,
                  "assets" => assets.map { |asset| { "name" => asset } }, "body" => body)
    end

  # Every device page asks for the list, so a burst of pilots must not spend the hourly GitHub allowance.
  test "the release list is asked of GitHub once, then read from the cache" do
    with_memory_cache do
      VCR.use_cassette("github/releases") do
        first = Release.all
        assert_equal first.map(&:tag), Release.all.map(&:tag)
      end
    end
  end
end
