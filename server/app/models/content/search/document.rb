class Content::Search::Document
  attr_reader :page

  def initialize(page)
    @page = page
    @title = Content::Search.fold(page.title)
    @description = Content::Search.fold(page.description)
    @paragraphs = page.markdown.split(/\n{2,}/).map { [ it.strip, Content::Search.fold(it) ] }
    @body = @paragraphs.map(&:last).join("\n\n")
  end

  def frequency(term)
    Content::Search::TITLE_WEIGHT * count(@title, term) + Content::Search::DESCRIPTION_WEIGHT * count(@description, term) +
      count(@body, term)
  end

  def excerpt(weights)
    @paragraphs.max_by { |_, folded| weights.sum { |term, weight| folded.match?(pattern(term)) ? weight : 0 } }.first
  end

  private
    def count(text, term) = text.scan(pattern(term)).size

    def pattern(term) = /(?<![[:alnum:]])#{Regexp.escape(term)}/
end
