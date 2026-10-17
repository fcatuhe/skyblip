class Ahoy::Visit < AnalyticsRecord
  include Filterable

  self.table_name = "ahoy_visits"

  has_many :events, class_name: "Ahoy::Event", dependent: :delete_all
  has_one :analytics_visit, class_name: "Analytics::Visit", foreign_key: :ahoy_visit_id, dependent: :destroy
  belongs_to :user, optional: true

  scope :filter_by_start_on, ->(value) { where(started_at: value.to_date.beginning_of_day..) }
  scope :filter_by_end_on,   ->(value) { where(started_at: ..value.to_date.end_of_day) }

  scope :filter_by_browser,  ->(value) { value == "unknown" ? where(browser: [ nil, "" ]) : where(browser: value) }
  scope :filter_by_os,       ->(value) { value == "unknown" ? where(os: [ nil, "" ])      : where(os: value) }
  scope :filter_by_device,   ->(value) { value == "unknown" ? where(device_type: [ nil, "" ]) : where(device_type: value) }

  scope :filter_by_country, ->(value) { value == "unknown" ? where(country: [ nil, "" ]) : where(country: value) }
  scope :filter_by_region,  ->(value) { value == "unknown" ? where(region: [ nil, "" ])  : where(region: value) }
  scope :filter_by_city,    ->(value) { value == "unknown" ? where(city: [ nil, "" ])    : where(city: value) }

  scope :filter_by_referrer, ->(value) {
    if value == "Direct"
      where(referring_domain: [ nil, "" ]).where(referrer: [ nil, "" ])
    else
      where(referring_domain: value)
        .or(where(referring_domain: [ nil, "" ]).where(arel_table[:referrer].matches("%#{sanitize_sql_like(value)}%")))
    end
  }
end
