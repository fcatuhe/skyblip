class Content::Search::Document
  attr_reader :page

  def initialize(page)
    @page = page
    @title = Content::Search.fold(page.title)
    @description = Content::Search.fold(page.description)
    @paragraphs = page.markdown.split(/\n{2,}/).map { [ it.strip, Content::Search.fold(it) ] }
    @body = @paragraphs.map(&:last).join("\n\n")
  end

  def frequency(pattern)
    Content::Search::TITLE_WEIGHT * @title.scan(pattern).size + Content::Search::DESCRIPTION_WEIGHT * @description.scan(pattern).size +
      @body.scan(pattern).size
  end

  def excerpt(weights)
    @paragraphs.max_by { |_, folded| weights.sum { |pattern, weight| folded.match?(pattern) ? weight : 0 } }.first
  end
end
