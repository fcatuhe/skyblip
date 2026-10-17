module Analytics
  class Visitor
    include Metrics
    include Metadata

    SOURCES = %w[all server browser].freeze
    METADATA_DISPLAY_KEYS = %i[referrer country region city browser os device].freeze

    attr_reader :visitor_token, :source

    def initialize(visitor_token:, source: "browser", page: 1)
      @visitor_token = visitor_token
      @source = SOURCES.include?(source) ? source : "browser"
      @page = page.to_i
    end

    def users
      @users ||= User.where(id: metadata[:all_user_ids]).index_by(&:id)
    end

    def timeline
      @timeline ||= Timeline.new(visitor_token: visitor_token, source: source, page: @page)
    end

    private

      def analytics_visits
        Analytics::Visit
          .joins(:ahoy_visit)
          .where(ahoy_visits: { visitor_token: visitor_token })
          .order(Arel.sql("ahoy_visits.started_at"))
      end
  end
end
