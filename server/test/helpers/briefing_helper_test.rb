require "test_helper"

class BriefingHelperTest < ActionView::TestCase
  test "a release is named by its tag, and a development build by its version with the build" do
    assert_equal "v0.8.0", version_name(release("v0.8.0", name: "skyBlip 0.8.0"))
    assert_equal "0.8.0+2405", version_name(release("dev-2405", name: "skyBlip dev 0.8.0+2405", prerelease: true))
  end

  private
    def release(tag, name:, prerelease: false)
      Release.new("tag_name" => tag, "name" => name, "prerelease" => prerelease, "created_at" => "2026-11-24T00:00:00Z", "assets" => [], "body" => nil)
    end
end
