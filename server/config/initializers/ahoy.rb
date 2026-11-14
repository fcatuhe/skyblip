class Ahoy::Store < Ahoy::DatabaseStore
  # INFO: fc 14nov26 the address is placed in memory while the request runs, so a visit keeps its city and not its address
  def track_visit(data)
    super(data.except(:ip).merge(place_of(data[:ip])))
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
    def place_of(ip)
      location = Geocoder.search(ip).first if ip && File.exist?(Geocoder.config.geoip2[:file])
      if location&.country.present?
        { country: location.country, country_code: location.country_code.presence, region: location.state.presence,
          city: location.city.presence, latitude: location.latitude, longitude: location.longitude }
      else
        {}
      end
    end

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

Ahoy.geocode = false

# INFO: fc 17oct26 renewed on each visit and capped at 13 months, the lifetime of an exempt analytics cookie
Ahoy.visitor_duration = Rails.configuration.x.tracker_lifetime
