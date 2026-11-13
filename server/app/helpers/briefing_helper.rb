module BriefingHelper
  DEVICE_NAMES = { "go" => "skyBlip Go", "bravo" => "skyBlip Bravo" }.freeze

  def briefing_words
    t("briefing").slice(:words, :notices, :set_notices, :capture_notices, :activation_notices, :browsers, :browsers_notices)
  end

  def device_name(slug)
    kind, tag = slug.split("-", 2)
    "#{DEVICE_NAMES.fetch(kind)} #{tag.upcase}"
  end
end
