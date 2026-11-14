require "test_helper"

class Admin::AgentsControllerTest < ActionDispatch::IntegrationTest
  include ConnectingAgents

  test "lists the connected agents, and disconnecting one ends its access" do
    tokens = connect(client_id: CLAUDE_CODE)

    get admin_agents_path
    assert_select "tbody th", text: "Claude Code"

    delete admin_agent_path(OAuth::Grant.sole)
    assert_redirected_to admin_agents_path
    mcp "tools/list", token: tokens.fetch("access_token")
    assert_response :unauthorized
  end
end
