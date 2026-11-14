class Briefing::PromptGlassesController < Briefing::ApplicationController
  def show
    redirect_to release_asset_path(prompt_tag, "embed.js", locale: nil)
  rescue Release::Unavailable => error
    Rails.logger.warn(error.message)
    head :service_unavailable
  end

  private
    def prompt_tag
      return Release::LOCAL_TAG if Release.local_simulator
      raise Release::Unavailable, "No local simulator build" if Rails.configuration.x.local_simulator_only

      Release.simulator&.tag or raise Release::Unavailable, "No release holds the simulator"
    end
end
