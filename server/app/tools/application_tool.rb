class ApplicationTool < MCP::Tool
  READING = { read_only_hint: true, destructive_hint: false, idempotent_hint: true, open_world_hint: false }.freeze
  DEVICE = { type: "string", description: "A device by its slug, go-00012a or bravo-3f2a, the address its pilot reads on the glass or the briefing's URL" }.freeze

  class << self
    private
      # INFO: fc 14nov26 security: an admin demoted after connecting an agent reads nothing more through it
      def admin(server_context)
        User.find_by(id: server_context.auth_info.subject)&.then { it if it.admin? }&.tap { Rails.event.set_context(user_id: it.id) }
      end

      def json(value) = text(JSON.pretty_generate(value))

      def text(body) = MCP::Tool::Response.new([ { type: "text", text: body } ])

      def refused(message) = MCP::Tool::Response.new([ { type: "text", text: message } ], error: true)

      def admins_only = refused("Only an admin may read skyBlip's events")
  end
end
