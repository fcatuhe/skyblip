module Analytics
  class OutboundClicks
    Link = Data.define(:href, :section, :text, :clicks, :visitors, :first_at, :last_at)
    Click = Data.define(:time, :href, :section, :text, :page, :visitor_token, :segment, :channel, :entry_path, :country)

    def initialize(visits, matching: nil)
      @visits = visits
      @matching = matching
    end

    def links(limit: nil)
      href = Ahoy::Event.property(:href)

      clicks.joins(:visit).group(href).limit(limit)
        .select(href.as("href"), maximum(:section).as("section"), maximum(:text).as("text"),
                Arel.star.count.as("clicks"), Ahoy::Visit.arel_table[:visitor_token].count(true).as("visitors"),
                Ahoy::Event.arel_table[:time].minimum.as("first_at"), Ahoy::Event.arel_table[:time].maximum.as("last_at"))
        .order(Arel.sql("clicks DESC, visitors DESC, href"))
        .map { to_link(it) }
    end

    def each(limit:)
      clicks.includes(visit: :analytics_visit).order(time: :desc, id: :desc).limit(limit).map { to_click(it) }.reverse
    end

    private
      def clicks
        scope = Ahoy::Event.outbound_clicks.where(visit_id: @visits.select(:ahoy_visit_id))

        if @matching.present?
          scope.where(Ahoy::Event.property(:href).matches("%#{Ahoy::Event.sanitize_sql_like(@matching)}%", "\\"))
        else
          scope
        end
      end

      def maximum(key) = Arel::Nodes::Max.new([ Ahoy::Event.property(key) ])

      def to_link(row)
        Link.new(href: row.href, section: row.section, text: row.text, clicks: row.clicks, visitors: row.visitors,
                 first_at: Analytics.parse_time(row.first_at), last_at: Analytics.parse_time(row.last_at))
      end

      def to_click(event)
        visit = event.visit.analytics_visit

        Click.new(time: event.time, href: event.props[:href], section: event.props[:section], text: event.props[:text],
                  page: event.props[:page], visitor_token: event.visit.visitor_token, segment: visit&.segment,
                  channel: visit&.channel, entry_path: visit&.entry_path, country: event.visit.country)
      end
  end
end
