class GetAnalyticsTool < AnalyticsTool
  tool_name "get_analytics"
  title "Read the audience of skyblip.eu"
  description "The admin Analytics overview: visitors, the visitors who activated a unit, visits, pageviews, bounce rate " \
              "and the average visit's seconds, the same counts by day, or by hour over two days or less, leaving out the " \
              "periods with no visit, the outbound links visitors clicked, and the " \
              "first #{Analytics::Overview::BREAKDOWN_LIMIT} rows of each breakdown, by path, entry and exit page, referrer, " \
              "channel, entry section, browser, system, device, country, region and city, each with its visitors, pageviews " \
              "and seconds. A row's filter value narrows this tool, list_visitors and list_outbound_clicks to its visits. " \
              "A visit counts in every path it viewed, and visitors do not add up across rows. #{FILTERING}"
  input_schema properties: FILTERS
  annotations(**READING)

  def self.call(server_context:, **filters)
    reading(server_context, **filters) do |filter_params|
      overview = Analytics::Overview.new(filter_params:)

      json filters: filter_params, metrics: overview.metrics, series: series_of(overview),
           outbound_links: overview.outbound_links.map(&:to_h), breakdowns: overview.breakdowns
    end
  end

  def self.series_of(overview)
    overview.series.then do |series|
      series[:visits].filter_map { |period, visits| { period:, **series.transform_values { it[period] } } if visits.positive? }
    end
  end
  private_class_method :series_of
end
