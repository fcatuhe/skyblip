class Ahoy::Event < AnalyticsRecord
  self.table_name = "ahoy_events"

  JAVASCRIPT_EVENT_PREFIX = "$"
  OWN_ORIGINS = [ Rails.configuration.x.public_url, Rails.configuration.x.public_url.sub("://", "://www.") ].freeze
  UNIT_ACTIVATED = "unit_activated"
  # INFO: fc 24nov26 every activation was tracked as registrations#create before unit_activated named its unit, so it keeps the earlier ones
  ACTIVATIONS = [ UNIT_ACTIVATED, "registrations#create" ].freeze

  scope :server_side, -> { where.not(arel_table[:name].matches("#{JAVASCRIPT_EVENT_PREFIX}%")) }
  scope :outbound_clicks, -> {
    where(name: "$click")
      .where(property(:href).matches_any(%w[http://% https://%]))
      .where(property(:href).does_not_match_all(OWN_ORIGINS.map { "#{it}/%" }))
  }
  scope :activations, -> { where(name: ACTIVATIONS) }
  scope :activating, ->(unit) { where(name: UNIT_ACTIVATED).where(property(:unit_id).eq(unit.id)) }

  belongs_to :visit
  belongs_to :user, optional: true

  serialize :properties, coder: JSON

  def self.property(key) = Arel::Nodes::NamedFunction.new("json_extract", [ arel_table[:properties], Arel::Nodes.build_quoted("$.#{key}") ])

  def props
    @props ||= (properties || {}).with_indifferent_access
  end
end
