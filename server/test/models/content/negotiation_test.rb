require "test_helper"

class Content::NegotiationTest < ActiveSupport::TestCase
  test "a browser and a client that names no type get HTML" do
    assert_equal :html, format_for("text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8")
    assert_equal :html, format_for("*/*")
    assert_equal :html, format_for(nil)
  end

  test "a coding agent that names Markdown gets it, even at the same quality as HTML or beside a wildcard" do
    assert_equal :md, format_for("text/markdown")
    assert_equal :md, format_for("text/markdown, text/html")
    assert_equal :md, format_for("text/markdown, text/html, */*")
  end

  test "qualities decide, and a quality of zero refuses the type" do
    assert_equal :html, format_for("text/html, text/markdown;q=0.5")
    assert_equal :md, format_for("text/html;q=0.5, text/markdown")
    assert_equal :html, format_for("text/markdown;q=0, text/html")
    assert_nil format_for("text/markdown;q=0")
  end

  test "a client that accepts neither HTML nor Markdown gets nothing" do
    assert_nil format_for("image/png")
    assert_nil format_for("application/json")
  end

  private
    def format_for(accept) = Content::Negotiation.new(accept).format
end
