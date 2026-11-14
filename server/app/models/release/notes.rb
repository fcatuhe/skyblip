class Release::Notes
  NEWS_HEADINGS = { en: "What's new", fr: "Nouveautés" }.freeze
  TECHNICAL_HEADING = "What's Changed".freeze
  PULL_REQUEST = %r{\A(?<title>.+) by @\S+ in (?<url>https://github\.com/#{Release::REPOSITORY}/pull/\d+)\z}
  Change = Data.define(:text, :url)

  def initialize(markdown)
    @items = items_by_heading(markdown.gsub(/<!--.*?-->/m, ""))
  end

  def news(locale)
    texts = @items.fetch(NEWS_HEADINGS[locale.to_sym], []).presence || @items.fetch(NEWS_HEADINGS.fetch(:en), [])
    texts.map { |text| Change.new(text:, url: nil) }
  end

  def technical
    @items.fetch(TECHNICAL_HEADING, []).map do |text|
      if (pull_request = PULL_REQUEST.match(text))
        Change.new(text: pull_request[:title], url: pull_request[:url])
      else
        Change.new(text:, url: nil)
      end
    end
  end

  private
    def items_by_heading(markdown)
      heading = nil
      markdown.lines(chomp: true).each_with_object({}) do |line, items|
        if (title = line[/\A##\s+(.+?)\s*\z/, 1])
          heading = title
        elsif heading && (item = line[/\A[*-]\s+(.+?)\s*\z/, 1])
          (items[heading] ||= []) << item
        end
      end
    end
end
