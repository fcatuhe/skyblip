module AnalyticsHelper
  CHART_TICKS = 3

  def analytics_default_path = admin_analytics_path(locale: nil, filters: { segment: "non_user" })

  def analytics_url_params(filter_params)
    { filters: filter_params.to_h.compact_blank.presence }.compact
  end

  def analytics_path_with(url, filter_params)
    query = analytics_url_params(filter_params)
    query.present? ? "#{url}?#{query.to_query}" : url
  end

  def analytics_period_links(url, filter_params)
    today = Date.current
    current = [ filter_params[:start_on]&.to_date, filter_params[:end_on]&.to_date ]

    safe_join(Analytics::PERIOD_PRESETS.map do |days|
      start_on = days.days.ago.to_date

      analytics_pill_link t("analytics.periods.#{days}"),
                          analytics_path_with(url, filter_params.merge(start_on:, end_on: today)),
                          active: current == [ start_on, today ]
    end)
  end

  def analytics_choice_links(url, filter_params, key:, values:)
    current = filter_params[key].to_s

    links = [ analytics_pill_link(t("analytics.all"), analytics_path_with(url, filter_params.except(key)), active: current.blank?) ]
    links += values.map do |value|
      analytics_pill_link t("analytics.#{key.to_s.pluralize}.#{value}"),
                          analytics_path_with(url, filter_params.merge(key => value)),
                          active: current == value
    end

    safe_join(links)
  end

  def analytics_chart_ticks(chart)
    marks = chart.marks

    if marks.size < CHART_TICKS
      marks
    else
      [ marks.first, marks[marks.size / 2], marks.last ]
    end
  end

  def analytics_mark_time(mark, hourly:)
    l(mark.time, format: hourly ? "%b %-d, %H:%M" : "%b %-d")
  end

  def analytics_mark_title(mark, hourly:)
    counts = mark.values.map { |name, value| "#{number_with_delimiter(value)} #{t("analytics.overview.#{name}").downcase}" }

    [ analytics_mark_time(mark, hourly:), counts.join(", ") ].join(" · ")
  end

  def analytics_chart_summary(chart)
    names = chart.lines.map { t("analytics.overview.#{it.name}") }
    bounds = [ chart.marks.first, chart.marks.last ].uniq.map { analytics_mark_time(it, hourly: chart.hourly?) }

    [ t("analytics.overview.chart_title"), names.join(", "), bounds.join(" - ") ].join(" · ")
  end

  def display_filters(filter_params)
    filter_params.slice(*Analytics::DISPLAY_FILTER_KEYS).compact_blank.to_h
  end

  def analytics_filter_label(key, count: 1)
    t("analytics.filters.#{key}", count:)
  end

  def analytics_filter_chip_label(key, value)
    "#{analytics_filter_label(key)}: #{value == "unknown" ? t("analytics.unknown") : value}"
  end

  def analytics_filter_link(url, filter_params, key:, value:, country_code: nil)
    label = value.present? ? analytics_label(key, value, country_code:) : t("analytics.unknown")

    link_to label, analytics_path_with(url, filter_params.merge(key => value.presence || "unknown"))
  end

  def analytics_label(key, value, country_code: nil)
    if key.to_sym.in?(%i[country region city]) && country_code.present?
      "#{country_flag(country_code)} #{value}"
    else
      value
    end
  end

  def analytics_visitor_link(visitor, users:, filter_params:)
    label = visitor.user_id ? analytics_user_name(visitor.user_id, users:) : visitor.visitor_token.to_s.first(8)

    link_to label, admin_analytics_visitor_path(visitor.visitor_token, analytics_url_params(filter_params)),
            class: class_names("analytics-visitor", "analytics-visitor--user": visitor.user_id)
  end

  def analytics_user_name(user_id, users:)
    users[user_id]&.name || t("analytics.unknown_user", id: user_id)
  end

  def format_duration(seconds)
    total = seconds.to_i
    days, rest = total.divmod(86_400)
    hours, rest = rest.divmod(3_600)
    minutes, secs = rest.divmod(60)

    if days.positive?
      format("%dd %02dh", days, hours)
    elsif hours.positive?
      format("%d:%02d:%02d", hours, minutes, secs)
    else
      format("%d:%02d", minutes, secs)
    end
  end

  private
    def analytics_pill_link(label, url, active:)
      link_to label, url, class: "admin-pill", "aria-current": ("true" if active), data: { turbo_action: "replace" }
    end

    def country_flag(country_code)
      country_code.upcase.each_char.map { (it.ord + 0x1F1A5).chr(Encoding::UTF_8) }.join
    end
end
