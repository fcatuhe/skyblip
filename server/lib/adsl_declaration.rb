class AdslDeclaration
  ASSET = "pages/ads-l/skyblip-go-ads-l-declaration.pdf"
  PATH = Rails.root.join("content_images", ASSET)
  TEMPLATE = Rails.root.join("lib/adsl_declaration/declaration.html.erb")
  STYLESHEET = Rails.root.join("lib/adsl_declaration/declaration.css")

  def generate
    require "ferrum"
    FileUtils.mkdir_p(PATH.dirname)

    LocalServer.serving(rack_app) do |base_url|
      browser = Ferrum::Browser.new
      browser.go_to("#{base_url}/declaration")
      browser.network.wait_for_idle
      browser.pdf(path: PATH.to_s, prefer_css_page_size: true)
      PATH
    ensure
      browser&.quit
    end
  end

  private
    def rack_app
      declaration = ->(_env) { [ 200, { "content-type" => "text/html; charset=utf-8" }, [ render_declaration ] ] }
      stylesheet = ->(_env) { [ 200, { "content-type" => "text/css" }, [ STYLESHEET.read ] ] }

      Rack::Builder.app do
        map("/declaration") { run declaration }
        map("/declaration.css") { run stylesheet }
        run Rails.application
      end
    end

    def render_declaration
      ApplicationController.renderer.render(inline: TEMPLATE.read)
    end
end
