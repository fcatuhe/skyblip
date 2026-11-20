class ListVisitorsTool < AnalyticsTool
  tool_name "list_visitors"
  title "List the visitors of skyblip.eu"
  description "The visitors whose visits pass the filters, last seen first, #{Analytics::Visitors::PER_PAGE} a page: the " \
              "visitor token get_visitor reads, the visits, views and seconds over the period and in the latest visit, the " \
              "browser and system, when the visitor was last seen, the user it signed in as, if any, and the page its latest " \
              "visit entered on. more says whether a next page holds older visitors. #{FILTERING}"
  input_schema properties: { **FILTERS, page: { type: "integer", minimum: 1, description: "The page, 1 unless said" } }
  annotations(**READING)

  def self.call(server_context:, page: 1, **filters)
    reading(server_context, page:, **filters) do |filter_params|
      visitors = Analytics::Visitors.new(filter_params:).page(page)

      json page: visitors.number, more: visitors.more?, visitors: visitors.summaries.map(&:to_h)
    end
  end
end
