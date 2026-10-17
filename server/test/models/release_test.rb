require "test_helper"

class ReleaseTest < ActiveSupport::TestCase
  test "a simulator release must have all three runtime files" do
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

  # GitHub returns dev-998, dev-1005, dev-1002 in this recording, not newest first.
  test "the shelf lists releases newest first whatever order GitHub returns" do
    releases = VCR.use_cassette("github/releases") { Release.all }

    assert_equal %w[dev-1005 dev-1002 dev-998], releases.map(&:tag)
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

  private
    def release(tag, name: "skyBlip", prerelease: false, published_at: "2026-10-17T00:00:00Z", assets: [])
      Release.new("tag_name" => tag, "name" => name, "prerelease" => prerelease, "published_at" => published_at,
                  "assets" => assets.map { |asset| { "name" => asset } })
    end
end
