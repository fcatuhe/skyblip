class Content::Markdown
  CONTROLS = "fieldset, form, button, input, select, textarea".freeze
  UNREAD = "svg, canvas, script, style, template, turbo-frame, [aria-hidden=true]".freeze

  def initialize(html)
    @html = html
  end

  def to_markdown
    markdown = ReverseMarkdown.convert(readable_fragment, unknown_tags: :bypass, github_flavored: true)

    "#{markdown.gsub("&nbsp;", "\u00A0").strip}\n"
  end

  private
    def readable_fragment
      Nokogiri::HTML5.fragment(@html).tap do |fragment|
        fragment.css(UNREAD).remove
        name_groups_a_radio_shows(fragment)
        fragment.css("button").each { it.replace(it.at_css("code") || "") }
        fragment.css(CONTROLS).remove
        fragment.css("details").each { it.name = "div" }
        fragment.css("summary").each { it.replace("<p><strong>#{CGI.escapeHTML(it.text.strip)}</strong></p>") }
        fragment.css("small, a + span").each { it.add_previous_sibling(" ") }
        fragment.css("a[href]").each { it["href"] = absolute_url(it["href"]) }
        fragment.css("img[src]").each { it["src"] = absolute_url(it["src"]) }
      end
    end

    def name_groups_a_radio_shows(fragment)
      fragment.css("label > input[type=radio][name][value]").each do |radio|
        label = CGI.escapeHTML(radio.parent.text.strip)

        fragment.css("tbody[data-#{radio["name"]}='#{radio["value"]}']").each do |group|
          group.prepend_child(%(<tr><td colspan="3"><strong>#{label}</strong></td></tr>))
        end
      end
    end

    def absolute_url(href)
      if href.start_with?("/") && !href.start_with?("//")
        path, anchor = href.split("#", 2)
        path = pages_by_path[path]&.markdown_path || path

        Content.canonical_url([ path, anchor ].compact.join("#"))
      else
        href
      end
    end

    def pages_by_path
      @pages_by_path ||= Content::Page.all.index_by(&:url_path)
    end
end
