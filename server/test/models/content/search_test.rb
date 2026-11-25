require "test_helper"

class Content::SearchTest < ActiveSupport::TestCase
  test "a question finds its page by its rarer words, with the paragraph that answers" do
    result = Content::Search.new("How do I see the traffic in SkyDemon?", locale: :en).results.first

    assert_equal "/phone", result.page.url_path
    assert_match(/SkyDemon/, result.excerpt)
    assert_operator result.excerpt.size, :<=, Content::Search::EXCERPT
  end

  test "a search reads one language, and finds a word with or without its accents" do
    assert_equal "/fr/ecrans", Content::Search.new("ecrans du Go", locale: :fr).results.first.page.url_path
    assert_equal "/fr/ecrans", Content::Search.new("Écrans du Go", locale: :fr).results.first.page.url_path
    assert Content::Search.new("batterie", locale: :fr).results.all? { it.page.locale == :fr }
  end

  test "a question with no word on any page finds nothing" do
    assert_empty Content::Search.new("zzyzx", locale: :en).results
    assert_empty Content::Search.new("?!", locale: :en).results
  end
end
