module AnalyticsTestHelper
  def clear_analytics
    Ahoy::Event.delete_all
    Ahoy::Visit.delete_all
    Analytics::Visit.delete_all
  end

  def browse(views, visitor:, started_at:, user: nil, referring_domain: nil, device_type: "Desktop")
    visit = Ahoy::Visit.create!(visit_token: SecureRandom.uuid, visitor_token: visitor, started_at:, user:,
                                referring_domain:, device_type:)
    time = started_at

    views.each do |path, seconds|
      Ahoy::Event.create!(name: "$view", visit_id: visit.id, time:, properties: { "page" => path })
      time += seconds.seconds
    end
    Ahoy::Event.create!(name: "$pagehide", visit_id: visit.id, time:, properties: {})

    Analytics::Visit.compute_now(visit)
    visit
  end
end
