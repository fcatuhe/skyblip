class GetVisitorTool < AnalyticsTool
  tool_name "get_visitor"
  title "Read a visitor's story on skyblip.eu"
  description "One visitor, by the token list_visitors or list_outbound_clicks gives: the users it signed in as, when it " \
              "was first and last seen, its first referrer, its latest place, browser, system and device, its visits, " \
              "views, server events and seconds, and the timeline of its latest #{Analytics::Visitor::Timeline::VISITS_PER_PAGE} " \
              "visits, oldest first: each visit's start and end, the pages it viewed, what it clicked, submitted and typed " \
              "into, when it left and came back, and with source server or all, each request the server answered. " \
              "page 2 reads the visits before. more says whether older visits remain. For admins only, and each read is logged."
  input_schema properties: {
    visitor_token: { type: "string", description: "The visitor's token, as list_visitors gives it" },
    source: { type: "string", enum: Analytics::Visitor::SOURCES, description: "Which events: browser unless said, server or all" },
    page: { type: "integer", minimum: 1, description: "The page of visits, 1 for the latest unless said" }
  }, required: [ "visitor_token" ]
  annotations(**READING)

  def self.call(server_context:, visitor_token:, source: "browser", page: 1)
    logged(server_context, visitor_token:, source:, page:) do
      visitor = Analytics::Visitor.new(visitor_token:, source:, page:)

      if visitor.metrics[:visits].positive?
        json visitor_token:, users: visitor.users.values.map { { id: it.id, name: it.name } },
             **visitor.metadata.except(:all_user_ids), **visitor.metrics,
             page: visitor.timeline.page, more: visitor.timeline.more?, timeline: timeline_of(visitor)
      else
        refused "No visitor has the token #{visitor_token.to_s.truncate(40)}"
      end
    end
  end

  def self.timeline_of(visitor)
    visitor.timeline.groups.flat_map { it[:events] }.reverse.map do |entry|
      { at: entry.display_time || entry.time, type: entry.type.to_s, label: (entry.label unless entry.session_marker?),
        detail: entry.detail, seconds: entry.duration }.compact
    end
  end
  private_class_method :timeline_of
end
