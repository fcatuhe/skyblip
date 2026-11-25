class McpsController < ActionController::API
  include ModelContextProtocol

  NAME = "eu.skyblip/website"
  VERSION = "1.0.0"
  TITLE = "skyBlip"
  DESCRIPTION = "Search and read skyblip.eu in five languages: skyBlip's ADS-L traffic devices, guides and blog."
  TOOLS = [ Content::ListPagesTool, Content::SearchPagesTool, Content::GetPageTool ].freeze
  INSTRUCTIONS = "skyBlip is open-source electronic conspicuity for recreational flying and air sports: skyBlip Go transmits " \
                 "its pilot's position in ADS-L, receives ADS-L and FLARM traffic and shows where to look, skyBlip Bravo receives " \
                 "ADS-B, and Tango pairs the two. These tools read skyblip.eu, its website, in Markdown: the devices, how to " \
                 "install and use them, ADS-L and the blog, in English, French, German, Italian and Spanish. " \
                 "Call search_pages to find the pages on a question, get_page to read one, and list_pages for every page in a language. " \
                 "Answer from the pages and link the page you answer from."

  # INFO: fc 25nov26 hosted connectors call from a few shared addresses, so the limit is per address but generous
  rate_limit to: 300, within: 1.minute, with: -> { head :too_many_requests, retry_after: 1.minute.to_i }

  private
    def transport
      MCP::Server::Transports::StreamableHTTPTransport.new(server, stateless: true, serve_subscriptions_listen: false, allowed_hosts:)
    end

    def server
      MCP::Server.new(name: NAME, version: VERSION, title: TITLE, description: DESCRIPTION, instructions: INSTRUCTIONS,
        tools: TOOLS, icons: ICONS, website_url: Rails.configuration.x.public_url)
    end
end
