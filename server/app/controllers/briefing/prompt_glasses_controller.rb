class Briefing::PromptGlassesController < Briefing::ApplicationController
  def show
    redirect_to release_asset_path(Release.prompt_glass_tag, "embed.js", locale: nil)
  rescue Release::Unavailable => error
    Rails.logger.warn(error.message)
    head :service_unavailable
  end
end
