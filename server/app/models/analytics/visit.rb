module Analytics
  class Visit < AnalyticsRecord
    self.table_name = "analytics_visits"

    include Filterable
    include Computable
    include Measurable

    belongs_to :ahoy_visit, class_name: "Ahoy::Visit"

    delegate :visitor_token, :started_at, :referring_domain,
             :browser, :os, :device_type, :country, :country_code,
             :region, :city, to: :ahoy_visit

    enum :segment, %w[non_user user internal].index_by(&:itself)
    enum :channel, %w[direct search social email referral].index_by(&:itself)
    enum :entry_section, %w[public briefing admin].index_by(&:itself), prefix: :section

    scope :filter_by_segment, ->(value) { where(segment: value) }
    scope :filter_by_channel, ->(value) { where(channel: value) }
    scope :filter_by_section, ->(value) { where(entry_section: value) }
    scope :filter_by_entry,   ->(value) { where(entry_path: value) }
    scope :filter_by_exit,    ->(value) { where(exit_path: value) }
    scope :filter_by_path,    ->(value) { where("EXISTS (SELECT 1 FROM json_each(analytics_visits.page_paths) WHERE json_each.value = ?)", value) }

    def self.filtered(filter_params)
      Filter.new(filter_params).scope
    end
  end
end
