require "test_helper"

class RenamedPagesTest < ActionDispatch::IntegrationTest
  test "the activation address the glass prints leads to the briefing app, from both languages" do
    get "/activate"
    assert_response :moved_permanently
    assert_redirected_to "/briefing/activate"

    get "/fr/activer"
    assert_response :moved_permanently
    assert_redirected_to "/briefing/activate"
  end

  test "the old update and settings pages lead to the briefing app, where the device is" do
    %w[/update /configure /manage /fr/mettre-a-jour /fr/regler /fr/mise-a-jour].each do |path|
      get path
      assert_redirected_to "/briefing", path
    end
  end

  test "the Go's old page address leads to its own, from both languages" do
    get "/features"
    assert_response :moved_permanently
    assert_redirected_to "/go"

    get "/fr/fonctionnalites"
    assert_redirected_to "/fr/go"
  end

  test "the old French briefing addresses lead to the same page, query kept" do
    get "/fr/briefing"
    assert_response :moved_permanently
    assert_redirected_to "/briefing"

    get "/fr/briefing/ownship?type=glider&rows=ff"
    assert_redirected_to "/briefing/ownship?type=glider&rows=ff"
  end
end
