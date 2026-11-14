module BriefingHelper
  DEVICE_NAMES = { "go" => "skyBlip Go", "bravo" => "skyBlip Bravo" }.freeze

  def briefing_words
    t("briefing").slice(:words, :notices, :set_notices, :capture, :capture_notices, :report, :activation_notices, :browsers, :browsers_notices)
  end

  def site_root_path
    root_path(locale: (I18n.locale unless I18n.locale == I18n.default_locale))
  end

  def device_name(slug)
    kind, tag = slug.split("-", 2)
    "#{DEVICE_NAMES.fetch(kind)} #{tag.upcase}"
  end
end
