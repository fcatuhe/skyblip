class Content::SearchPagesTool < ApplicationTool
  tool_name "search_pages"
  title "Search the pages of skyblip.eu"
  description "The pages of skyblip.eu in one language that best match the words of a question, best first, at most " \
              "#{Content::Search::LIMIT}: each page as list_pages gives it, with the paragraph that matches best. " \
              "Search in the language of the question, then read a page whole with get_page."
  input_schema properties: {
    query: { type: "string", minLength: 1, maxLength: 200, description: "The words to look for, such as SkyDemon Bluetooth or batterie" },
    language: LANGUAGE
  }, required: [ "query" ]
  annotations(**READING)

  def self.call(server_context:, query:, language: I18n.default_locale.to_s)
    json(Content::Search.new(query, locale: language).results.map do |result|
      Content::ListPagesTool.facts_of(result.page).merge(excerpt: result.excerpt)
    end)
  end
end
