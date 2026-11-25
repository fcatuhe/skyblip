module BriefingHelper
  DEVICE_NAMES = { "go" => "skyBlip Go", "bravo" => "skyBlip Bravo" }.freeze

  def briefing_words
    t("briefing").slice(:words, :notices, :set_notices, :capture, :capture_notices, :report, :activation_notices, :browsers, :browsers_notices,
      :setup, :setup_notices, :tango, :pair_notices)
  end

  def site_root_path
    root_path(locale: (I18n.locale unless I18n.locale == I18n.default_locale))
  end

  def device_name(slug)
    kind, tag = slug.split("-", 2)
    "#{DEVICE_NAMES.fetch(kind)} #{tag.upcase}"
  end

  def firmware_label(release, file)
    if Release.kind_of(file) == "full"
      "#{release.name}, #{t("firmwares.full")}"
    else
      release.name
    end
  end

  def version_name(release)
    release.stable? ? release.tag : release.version
  end

  def aircraft_address_hint(slug)
    t("settings.aircraft_address_hints.#{slug.split("-").first}")
  end
end
