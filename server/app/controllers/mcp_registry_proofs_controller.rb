class McpRegistryProofsController < ActionController::API
  include Tracing

  def show
    if (key = Rails.configuration.x.mcp_registry_public_key.presence)
      render plain: "v=MCPv1; k=ed25519; p=#{key}"
    else
      head :not_found
    end
  end
end
