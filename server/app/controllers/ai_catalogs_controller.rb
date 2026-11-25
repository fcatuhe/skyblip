class AiCatalogsController < ActionController::API
  include Tracing

  MEDIA_TYPE = "application/ai-catalog+json"

  def show
    response.headers["Access-Control-Allow-Origin"] = "*"
    expires_in 1.hour, public: true

    render json: catalog, content_type: MEDIA_TYPE
  end

  private
    def catalog
      { specVersion: "0.9", host: { displayName: "skyBlip", identifier: URI(Rails.configuration.x.public_url).host },
        entries: [ { identifier: "urn:air:skyblip.eu:website:mcp", type: Mcps::ServerCardsController::MEDIA_TYPE,
                     displayName: McpsController::TITLE, description: McpsController::DESCRIPTION,
                     url: Content.canonical_url(mcp_server_card_path) } ] }
    end
end
