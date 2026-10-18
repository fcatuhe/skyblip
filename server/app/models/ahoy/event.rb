class Ahoy::Event < AnalyticsRecord
  self.table_name = "ahoy_events"

  JAVASCRIPT_EVENT_PREFIX = "$"

  scope :server_side, -> { where.not(arel_table[:name].matches("#{JAVASCRIPT_EVENT_PREFIX}%")) }

  belongs_to :visit
  belongs_to :user, optional: true

  serialize :properties, coder: JSON

  def props
    @props ||= (properties || {}).with_indifferent_access
  end
end
