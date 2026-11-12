class Briefing::ManifestsController < Briefing::ApplicationController
  def show
    render json: {
      id: "/briefing",
      name: t("briefing.app.name"),
      short_name: "skyBlip",
      icons: [
        { src: "/icon.png", type: "image/png", sizes: "512x512" },
        { src: "/icon.svg", type: "image/svg+xml", sizes: "any" }
      ],
      start_url: briefing_root_path,
      scope: briefing_root_path,
      display: "standalone",
      lang: I18n.locale,
      description: t("briefing.app.description"),
      theme_color: Rails.configuration.x.theme_color,
      background_color: Rails.configuration.x.theme_color
    }
  end
end
