require "test_helper"

class ReleaseTest < ActiveSupport::TestCase
  test "a simulator release must have all three runtime files, the scenarios aside" do
    assert_not release("dev-1005", assets: %w[embed.js]).simulator?

    complete = release("dev-1005", assets: Release::SIMULATOR_FILES)
    assert complete.simulator?
    assert complete.development?
  end

  test "firmware offerings exclude arbitrary release attachments" do
    stable = release("v0.1.0", name: nil, assets: %w[skyblip-go-0.1.0.signed.bin skyblip-go-0.1.0.full.signed.bin SHA256SUMS evil.js])

    assert_equal "v0.1.0", stable.name
    assert_equal "release", stable.channel
    assert_equal %w[skyblip-go-0.1.0.signed.bin skyblip-go-0.1.0.full.signed.bin], stable.firmware_files
  end

  test "each product's tag prefix names the product its release offers images of, and no other" do
    assets = %w[skyblip-go-dev-1010.signed.bin skyblip-bravo-dev-1010.signed.bin skyblip-bravo-dev-1010.full.signed.bin skyblip-tango-dev-1010.signed.bin]
    go = release("dev-1010", assets:)
    bravo = release("bravo-dev-1010", assets:)

    assert_equal [ "skyblip_go", %w[skyblip-go-dev-1010.signed.bin] ], [ go.product, go.firmware_files ]
    assert_equal [ "skyblip_bravo", %w[skyblip-bravo-dev-1010.signed.bin skyblip-bravo-dev-1010.full.signed.bin] ], [ bravo.product, bravo.firmware_files ]
    assert_equal %w[skyblip_bravo skyblip_bravo], bravo.firmware_files.map { |file| Release.product_of(file) }
    assert_nil Release.product_of("embed.js")
  end

  test "a product's tags read as the Go's do once their prefix is off" do
    assert release("bravo-v0.2.0").stable?
    assert release("bravo-dev-1010", prerelease: true).main_development?
    assert_equal "development", release("bravo-dev-1010-feat-x", prerelease: true).channel
    assert_equal "skyblip_go", release("dev-1010-bravo-fix", prerelease: true).product
    assert_not Release::TAG.match?("tango-v0.1.0")
  end

  # GitHub returns dev-998, dev-1005, dev-1002 in this recording, not newest first.
  test "the shelf lists releases newest first whatever order GitHub returns" do
    releases = VCR.use_cassette("github/releases") { Release.all }

    assert_equal %w[dev-1005 dev-1002 dev-998], releases.map(&:tag)
  end

  test "the shelf keeps the newest stable release and every development build, never a release candidate" do
    older_stable = release("v0.1.0", published_at: "2026-10-01T00:00:00Z")
    newer_stable = release("v0.2.0", published_at: "2026-10-10T00:00:00Z")
    candidate = release("v0.3.0-rc1", prerelease: true, published_at: "2026-10-12T00:00:00Z")
    main = release("dev-1005", prerelease: true, published_at: "2026-10-14T00:00:00Z")
    branch = release("dev-1010-feat-x", prerelease: true, published_at: "2026-10-15T00:00:00Z")

    assert_equal %w[v0.2.0 dev-1010-feat-x dev-1005], Release.shelf([ older_stable, main, candidate, branch, newer_stable ]).map(&:tag)
  end

  test "the shelf keeps each product's newest stable release, whichever product released last" do
    go = release("v0.2.0", published_at: "2026-10-10T00:00:00Z")
    older_bravo = release("bravo-v0.1.0", published_at: "2026-10-11T00:00:00Z")
    newer_bravo = release("bravo-v0.2.0", published_at: "2026-10-12T00:00:00Z")
    bravo_main = release("bravo-dev-1005", prerelease: true, published_at: "2026-10-14T00:00:00Z")

    assert_equal %w[bravo-v0.2.0 v0.2.0 bravo-dev-1005], Release.shelf([ go, bravo_main, older_bravo, newer_bravo ]).map(&:tag)
  end

  test "the firmware shelf lists every slim image newest first, then every full one" do
    older = release("dev-1002", published_at: "2026-10-14T00:00:00Z", assets: %w[skyblip-go-dev-1002.full.signed.bin skyblip-go-dev-1002.signed.bin])
    newer = release("dev-1005", published_at: "2026-10-15T00:00:00Z", assets: %w[skyblip-go-dev-1005.full.signed.bin skyblip-go-dev-1005.signed.bin])

    assert_equal %w[skyblip-go-dev-1005.signed.bin skyblip-go-dev-1002.signed.bin skyblip-go-dev-1005.full.signed.bin skyblip-go-dev-1002.full.signed.bin],
                 Release.firmware_shelf([ older, newer ]).map(&:last)
  end

  test "the simulator prefers the newest stable release, then the newest main build" do
    older_stable = release("v0.1.0", published_at: "2026-10-01T00:00:00Z", assets: Release::SIMULATOR_FILES)
    newer_stable = release("v0.2.0", published_at: "2026-10-10T00:00:00Z", assets: Release::SIMULATOR_FILES)
    candidate = release("v0.3.0-rc1", prerelease: true, published_at: "2026-10-12T00:00:00Z", assets: Release::SIMULATOR_FILES)
    branch = release("dev-1010-feat-x", prerelease: true, published_at: "2026-10-15T00:00:00Z", assets: Release::SIMULATOR_FILES)
    main = release("dev-1005", prerelease: true, published_at: "2026-10-14T00:00:00Z", assets: Release::SIMULATOR_FILES)

    assert_equal "v0.2.0", Release.simulator([ branch, candidate, newer_stable, main, older_stable ]).tag
    assert_equal "dev-1005", Release.simulator([ branch, candidate, main ]).tag
    assert_nil Release.simulator([ branch ])
  end

  # Until the simulator page offers Bravo (#180), it runs and names the Go's releases only.
  test "the simulator follows the Go's releases" do
    go = release("v0.2.0", published_at: "2026-10-10T00:00:00Z", assets: Release::SIMULATOR_FILES)
    bravo = release("bravo-v0.1.0", published_at: "2026-10-12T00:00:00Z", assets: Release::SIMULATOR_FILES)
    bravo_main = release("bravo-dev-1005", prerelease: true, published_at: "2026-10-14T00:00:00Z", assets: Release::SIMULATOR_FILES)

    assert_equal "v0.2.0", Release.simulator([ bravo, go, bravo_main ]).tag
    assert_nil Release.simulator([ bravo, bravo_main ])
  end

  private
    def release(tag, name: "skyBlip", prerelease: false, published_at: "2026-10-17T00:00:00Z", assets: [])
      Release.new("tag_name" => tag, "name" => name, "prerelease" => prerelease, "published_at" => published_at,
                  "assets" => assets.map { |asset| { "name" => asset } })
    end

  # Every device page asks for the list, so a burst of pilots must not spend the hourly GitHub allowance.
  test "the release list is asked of GitHub once, then read from the cache" do
    cache, Rails.cache = Rails.cache, ActiveSupport::Cache::MemoryStore.new
    VCR.use_cassette("github/releases") do
      first = Release.all
      assert_equal first.map(&:tag), Release.all.map(&:tag)
    end
  ensure
    Rails.cache = cache
  end
end
