Geocoder.configure(
  ip_lookup: :geoip2,
  geoip2: {
    file: Rails.root.join("storage/GeoLite2-City.mmdb").to_s
  }
)
