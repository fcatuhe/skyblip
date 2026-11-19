class ListOutboundClicksTool < AnalyticsTool
  MAX_LIMIT = 2_000

  tool_name "list_outbound_clicks"
  title "List the clicks on links to other sites"
  description "The clicks skyblip.eu's visitors made on links to other sites, the affiliate links of the buy and mount " \
              "pages among them, one line per link, most clicked first: the address, the section it sits in, the product " \
              "on the buy page (go, go-plus, bravo), the link's words, its clicks, its distinct visitors, and its first and " \
              "last click. With each, every click instead, oldest first, with the visitor, the segment, the channel, the " \
              "entry page and the country of its visit. A middle click counts, a link opened from a long press does not. " \
              "The filters narrow the visits the clicks come from, as in get_analytics. #{FILTERING}"
  input_schema properties: {
    **FILTERS,
    href: { type: "string", description: "Keep the links whose address holds this, such as amazon, lilygo.cc/skyblip-go or B00EaKI0W" },
    each: { type: "boolean", description: "List each click rather than one line per link" },
    limit: { type: "integer", minimum: 1, maximum: MAX_LIMIT, description: "How many clicks with each, the latest, #{Event::LIMIT} unless said" }
  }
  annotations(**READING)

  def self.call(server_context:, href: nil, each: false, limit: Event::LIMIT, **filters)
    reading(server_context, href:, each: (true if each), **filters) do |filter_params|
      clicks = Analytics::OutboundClicks.new(Analytics::Visit.filtered(filter_params), matching: href)

      json each ? clicks.each(limit: limit.to_i.clamp(1, MAX_LIMIT)).map(&:to_h) : clicks.links.map(&:to_h)
    end
  end
end
