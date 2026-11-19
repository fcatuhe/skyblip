require "test_helper"

class Content::MarkdownTest < ActiveSupport::TestCase
  test "links point at the absolute Markdown of a page, and at the absolute address of anything else" do
    markdown = to_markdown(<<~HTML)
      <p><a href="/radar#shapes">Radar</a>, <a href="/fr">Accueil</a>, <a href="/briefing">Briefing</a> and <a href="https://github.com/fcatuhe/skyblip">GitHub</a>.</p>
      <img src="/assets/radar.png" alt="The radar">
    HTML

    assert_includes markdown, "[Radar](https://skyblip.eu/radar.md#shapes)"
    assert_includes markdown, "[Accueil](https://skyblip.eu/fr/index.md)"
    assert_includes markdown, "[Briefing](https://skyblip.eu/briefing)"
    assert_includes markdown, "[GitHub](https://github.com/fcatuhe/skyblip)"
    assert_includes markdown, "![The radar](https://skyblip.eu/assets/radar.png)"
  end

  test "controls and drawings are left out, but a code a button shows is kept" do
    markdown = to_markdown(<<~HTML)
      <p>Order with code <button><code>5GO3A5DD</code><span>Copy</span></button> today.</p>
      <svg><text>Diagram</text></svg>
      <turbo-frame id="simulator">Loading</turbo-frame>
      <form><label>Callsign <input name="callsign"></label></form>
    HTML

    assert_equal "Order with code `5GO3A5DD` today.\n", markdown
  end

  test "a question opens its answer, and a no-break space stays a character" do
    markdown = to_markdown("<details><summary>Is it certified?</summary><p>No, 3&nbsp;km.</p></details>")

    assert_equal "**Is it certified?**\n\nNo, 3\u00A0km.\n", markdown
  end

  # The Markdown shows every country at once, where the page shows the one its radio picks.
  test "a group a radio shows is named after the radio" do
    markdown = to_markdown(<<~HTML)
      <fieldset><label><input type="radio" name="country" value="fr">France</label></fieldset>
      <table><tbody data-country="fr"><tr><th>Go</th><td>€68</td></tr></tbody></table>
    HTML

    assert_includes markdown, "| **France** |\n| Go | €68 |"
    assert_not_includes markdown, "radio"
  end

  private
    def to_markdown(html) = Content::Markdown.new(html).to_markdown
end
