class Ahoy::Store < Ahoy::DatabaseStore
  def track_visit(data)
    super
    ahoy.send(:set_cookie, "ahoy_visitor", ahoy.visitor_token, Ahoy.visitor_duration)
  end

  def track_event(data)
    if well_formed?(data[:properties])
      super
      Analytics::Visit.compute_later(visit) if visit
    end
  end

  # INFO: fc 17oct26 the sign_in event records the user before the backfill reads the browser's users
  def authenticate(data)
    super
    ahoy.track "sign_in"
    Analytics::Visit::BackfillVisitorSegmentJob.perform_later(ahoy.visitor_token) if ahoy.visitor_token
  end

  private
    def well_formed?(properties)
      case properties
      when nil then true
      when Hash then properties.values_at("page", "controller", :controller).all? { it.nil? || it.is_a?(String) }
      else false
      end
    end
end

Ahoy.user_method = ->(_controller) { Current.user }
Ahoy.server_side_visits = :when_needed
Ahoy.api_only = Rails.env.local?

# INFO: fc 17oct26 renewed on each visit and capped at 13 months, the lifetime of an exempt analytics cookie
Ahoy.visitor_duration = 13.months
