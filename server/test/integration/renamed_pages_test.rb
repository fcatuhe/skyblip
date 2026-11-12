require "test_helper"

class RenamedPagesTest < ActionDispatch::IntegrationTest
  test "the activation address the glass prints leads to the briefing app, in both languages" do
    get "/activate"
    assert_response :moved_permanently
    assert_redirected_to "/briefing/activate"

    get "/fr/activer"
    assert_response :moved_permanently
    assert_redirected_to "/fr/briefing/activate"
  end

  test "the old update and settings pages lead to the briefing app, where the device is" do
    %w[/update /configure /manage].each do |path|
      get path
      assert_redirected_to "/briefing", path
    end

    %w[/fr/mettre-a-jour /fr/regler /fr/mise-a-jour].each do |path|
      get path
      assert_redirected_to "/fr/briefing", path
    end
  end
end
