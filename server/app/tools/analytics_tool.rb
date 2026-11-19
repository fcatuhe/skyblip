class AnalyticsTool < ApplicationTool
  MAX_DAYS = Rails.configuration.x.retention.in_days.to_i
  DAYS = 30
  FILTERS = {
    days: { type: "integer", minimum: 1, maximum: MAX_DAYS, description: "The last days, #{DAYS} unless said or start_on is given" },
    start_on: { type: "string", format: "date", description: "The first day, YYYY-MM-DD, in place of days" },
    end_on: { type: "string", format: "date", description: "The last day, YYYY-MM-DD, today unless said" },
    segment: { type: "string", enum: [ *Analytics::Visit.segments.keys, "all" ],
               description: "Whose visits: non_user unless said, which leaves out admins, the browsers they signed in from, and users" },
    section: { type: "string", enum: Analytics::Visit.entry_sections.keys, description: "The part of the site the visit entered" },
    channel: { type: "string", enum: Analytics::Visit.channels.keys, description: "Where the visit came from" },
    path: { type: "string", description: "A page the visit viewed, such as /buy" },
    entry: { type: "string", description: "The page the visit entered on" },
    exit: { type: "string", description: "The page the visit left from" },
    outbound: { type: "string", description: "A link to another site the visit clicked, its whole address" },
    referrer: { type: "string", description: "The referring domain, such as google.com, or Direct" },
    browser: { type: "string", description: "The browser, such as Chrome or Safari" },
    os: { type: "string", description: "The system, such as Windows, Mac, iOS or Android" },
    device: { type: "string", description: "The device type: Desktop, Mobile or Tablet" },
    country: { type: "string", description: "The country, in English, such as France" },
    region: { type: "string", description: "The region, such as Bretagne" },
    city: { type: "string", description: "The city" }
  }.freeze
  FILTERING = "Every filter value may be unknown, for the visits that lack it. For admins only, and each read is logged."

  class << self
    private
      def reading(server_context, **arguments)
        if (filter_params = filter_params_from(arguments))
          logged(server_context, **arguments) { yield filter_params }
        else
          refused "start_on and end_on are days written YYYY-MM-DD"
        end
      end

      def logged(server_context, **arguments)
        if (reader = admin(server_context))
          Event.record_now "analytics.listed", { by_id: reader.id, tool: tool_name, **arguments.compact_blank }
          yield
        else
          admins_only
        end
      end

      def filter_params_from(arguments)
        filters = arguments.slice(*Analytics::FILTER_KEYS).compact_blank
        filters[:segment] = arguments.fetch(:segment, "non_user").then { it unless it == "all" }
        filters[:start_on] = filters[:start_on] ? Date.iso8601(filters[:start_on]) : arguments.fetch(:days, DAYS).to_i.clamp(1, MAX_DAYS).days.ago.to_date
        filters[:end_on] = filters[:end_on] ? Date.iso8601(filters[:end_on]) : Date.current
        filters.compact
      rescue Date::Error
        nil
      end
  end
end
