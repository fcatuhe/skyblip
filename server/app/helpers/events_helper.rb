module EventsHelper
  def event_time(time) = l(time, format: "%b %-d, %H:%M:%S")

  def support_code(browser) = (browser.present? ? "#{browser.first(4)}-#{browser.from(4)}" : nil)

  def event_facts(event)
    event.payload.map { |key, value| "#{key}=#{value.is_a?(String) ? value : value.to_json}" }.join(" ")
  end

  def insight_value(value)
    case value
    when Hash
      if value.empty?
        tag.span("none", class: "quiet")
      else
        tag.dl(class: "admin-details admin-details--nested") do
          safe_join(value.map { |key, inner| tag.dt(key.to_s.humanize) + tag.dd(insight_value(inner)) })
        end
      end
    when Array
      value.empty? ? tag.span("none", class: "quiet") : tag.ul(safe_join(value.map { tag.li(insight_value(it)) }))
    when nil then tag.span("unknown", class: "quiet")
    else value.to_s
    end
  end
end
