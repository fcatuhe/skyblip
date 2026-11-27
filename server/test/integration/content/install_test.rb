require "test_helper"

class Content::InstallTest < ActionDispatch::IntegrationTest
  # GitHub lists the dev pre-releases above the latest release, so the plain releases page offers a dev build first.
  test "the manual install downloads from the latest release in every language" do
    %i[en fr de it es].each do |locale|
      get Content::Page.find_by(base_slug: "install", locale:).url_path

      assert_select "#manual + p a[href=?]", "#{Rails.configuration.x.repository_url}/releases/latest"
      assert_select "a[href=?]", "#{Rails.configuration.x.repository_url}/releases", count: 0
    end
  end
end
