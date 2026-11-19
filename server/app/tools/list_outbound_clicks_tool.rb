class ListOutboundClicksTool < ApplicationTool
  MAX_DAYS = Rails.configuration.x.retention.in_days.to_i
  MAX_LIMIT = 2_000
  SEGMENTS = [ *Analytics::Visit.segments.keys, "all" ].freeze

  tool_name "list_outbound_clicks"
  title "List the clicks on links to other sites"
  description "The clicks skyblip.eu's visitors made on links to other sites, the affiliate links of the buy and mount " \
              "pages among them, one line per link, most clicked first: the address, the section it sits in, the product " \
              "on the buy page (go, go-plus, bravo), the link's words, its clicks, its distinct visitors, and its first and " \
              "last click. With each, every click instead, oldest first, with the visitor, the segment, the channel, the " \
              "entry page and the country of its visit. A middle click counts, a link opened from a long press does not. " \
              "For admins only, and each read is logged."
  input_schema properties: {
    days: { type: "integer", minimum: 1, maximum: MAX_DAYS, description: "How far back, 30 unless said" },
    segment: { type: "string", enum: SEGMENTS,
               description: "Whose visits: non_user unless said, which leaves out admins, the browsers they signed in from, and users" },
    href: { type: "string", description: "Keep the links whose address holds this, such as amazon, lilygo.cc/skyblip-go or B00EaKI0W" },
    each: { type: "boolean", description: "List each click rather than one line per link" },
    limit: { type: "integer", minimum: 1, maximum: MAX_LIMIT, description: "How many clicks with each, the latest, #{Event::LIMIT} unless said" }
  }
  annotations(**READING)

  def self.call(server_context:, days: 30, segment: "non_user", href: nil, each: false, limit: Event::LIMIT)
    if (reader = admin(server_context))
      days = days.to_i.clamp(1, MAX_DAYS)
      Event.record_now "analytics.listed", { by_id: reader.id, days:, segment:, href:, each: (true if each) }.compact_blank
      clicks = Analytics::OutboundClicks.new(visits(days:, segment:), matching: href)

      json each ? clicks.each(limit: limit.to_i.clamp(1, MAX_LIMIT)).map(&:to_h) : clicks.links.map(&:to_h)
    else
      admins_only
    end
  end

  def self.visits(days:, segment:)
    Analytics::Visit.filtered({ start_on: days.days.ago.to_date, end_on: Date.current, segment: (segment unless segment == "all") }.compact)
  end
  private_class_method :visits
end
