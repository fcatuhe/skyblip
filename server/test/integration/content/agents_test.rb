require "test_helper"

class Content::AgentsTest < ActionDispatch::IntegrationTest
  test "every page answers in Markdown at its .md address, naming its HTML as canonical" do
    Content::Page.all.each do |page|
      get page.markdown_path

      assert_response :success, page.markdown_path
      assert_equal "text/markdown", response.media_type
      assert_match(/^# \S/, response.body, page.markdown_path)
      assert_equal %(<#{Content.canonical_url(page.url_path)}>; rel="canonical", <#{page.url_path}>; rel="alternate"; type="text/html"), response.headers["Link"]
    end
  end

  test "a page's HTML points at its Markdown in the head and in a Link header" do
    get "/fr/a-propos"

    assert_select "link[rel=alternate][type='text/markdown'][href=?]", "https://skyblip.eu/fr/a-propos.md"
    assert_includes response.headers["Link"], %(</fr/a-propos.md>; rel="alternate"; type="text/markdown")
    assert_includes response.headers["Vary"], "Accept"
  end

  test "the same address answers in Markdown to a client that asks for it, and in HTML to a browser" do
    get "/radar", headers: { "Accept" => "text/markdown, text/html, */*" }
    assert_equal "text/markdown", response.media_type
    assert_includes response.headers["Vary"], "Accept"

    get "/", headers: { "Accept" => "text/markdown" }
    assert_equal "text/markdown", response.media_type

    get "/radar", headers: { "Accept" => "text/html,application/xhtml+xml,*/*;q=0.8" }
    assert_equal "text/html", response.media_type

    get "/radar", headers: { "Accept" => "image/png" }
    assert_response :not_acceptable
  end

  test "a Markdown request for no page answers in Markdown and points at its language's llms.txt" do
    [ [ "/nope.md", {}, "/llms.txt" ], [ "/fr/about.md", {}, "/fr/llms.txt" ], [ "/nope", { "Accept" => "text/markdown" }, "/llms.txt" ] ].each do |path, headers, llms|
      get path, headers: headers

      assert_response :not_found, path
      assert_equal "text/markdown", response.media_type
      assert_includes response.body, "(https://skyblip.eu#{llms})"
    end

    get "/nope"
    assert_response :not_found
    assert_equal "text/html", response.media_type
  end

  test "llms.txt links the Markdown of pages that exist, and every page links llms.txt" do
    get "/llms.txt"

    assert_response :success
    assert response.body.start_with?("# skyBlip\n")
    links = response.body.scan(%r{\(https://skyblip\.eu(/[^)]*\.md)\)}).flatten
    assert_operator links.size, :>=, 30
    assert_equal links.sort, (Content::Page.all.map(&:markdown_path) & links).sort

    get "/radar"
    assert_select "footer a[href='/llms.txt']"
  end

  test "each language has its llms.txt, which lists the pages not translated yet in English and names the MCP server" do
    get "/fr/llms.txt"

    assert_response :success
    assert_includes response.body, "(https://skyblip.eu/fr/a-propos.md)"
    assert_includes response.body, "(https://skyblip.eu/ads-l/nmea.md)"
    assert_includes response.body, "(https://skyblip.eu/fr/llms-full.txt)"
    assert_includes response.body, "https://skyblip.eu/mcp,"
    assert_includes response.body, "- [English](https://skyblip.eu/llms.txt)"

    get "/fr/a-propos"
    assert_select "footer a[href='/fr/llms.txt']"
  end

  test "each language's llms-full.txt holds its own pages" do
    get "/fr/llms-full.txt"

    assert_includes response.body, "Source: https://skyblip.eu/fr/a-propos\n"
    assert_includes response.body, "Source: https://skyblip.eu/ads-l/nmea\n"
    assert_not_includes response.body, "Source: https://skyblip.eu/about\n"
  end

  test "each language has an Atom feed of its pages, latest change first, that every page links" do
    get "/fr/feed.atom"

    assert_response :success
    assert_equal "application/atom+xml", response.media_type
    feed = Nokogiri::XML(response.body).remove_namespaces!
    updated = feed.xpath("//entry/updated").map(&:text)
    assert_equal Content::Page.read_in(:fr).size, updated.size
    assert_equal updated.sort.reverse, updated
    assert_equal "https://skyblip.eu/fr/a-propos.md", feed.at_xpath("//entry[id='https://skyblip.eu/fr/a-propos']/link[@type='text/markdown']/@href").text

    get "/fr/a-propos"
    assert_select "link[rel=alternate][type='application/atom+xml'][href=?]", "https://skyblip.eu/fr/feed.atom"
  end

  test "llms-full.txt holds every English page but the legal notice" do
    get "/llms-full.txt"

    assert_response :success
    assert_includes response.body, "Source: https://skyblip.eu/radar\n\n[Guides](https://skyblip.eu/guides.md)\n\n# Read the radar, alarm and Nearby list."
    assert_includes response.body, "Source: https://skyblip.eu/ads-l/nmea\n"
    assert_not_includes response.body, "Source: https://skyblip.eu/legal-notice"
    assert_not_includes response.body, "Source: https://skyblip.eu/fr"
  end
end
