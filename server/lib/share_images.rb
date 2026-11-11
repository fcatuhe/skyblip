class ShareImages
  WIDTH = 1200
  HEIGHT = 630
  CARDS = {
    "go" => { name: "Go", protocols: "ADS-L · FLARM", photos: %w[pages/index/skyblip-go-techo.avif] },
    "radar" => { name: "Go", protocols: "ADS-L · FLARM", screen: "pages/skyblip-go/radar.png" },
    "bravo" => { name: "Bravo", protocols: "ADS-B 1090 MHz", photos: %w[pages/skyblip-bravo/seeed-x1.avif], in_development: true },
    "tango" => { name: "Tango", protocols: "ADS-B · ADS-L · FLARM", photos: %w[pages/index/skyblip-go-techo.avif pages/skyblip-bravo/seeed-x1.avif], in_development: true }
  }
  PHOTO_CLASSES = { "pages/skyblip-bravo/seeed-x1.avif" => "photo-x1" }
  DIRECTORY = Rails.root.join("content_images/share")
  TEMPLATE = Rails.root.join("lib/share_images/card.html.erb")
  STYLESHEET = Rails.root.join("lib/share_images/card.css")

  def self.filename(name, locale)
    [ name, (locale if localized?(name)), "jpg" ].compact.join(".")
  end

  def self.localized?(name) = CARDS.fetch(name).key?(:in_development)

  def generate
    require "ferrum"
    FileUtils.mkdir_p(DIRECTORY)

    serving do |base_url|
      browser = Ferrum::Browser.new
      browser.set_viewport(width: WIDTH, height: HEIGHT, scale_factor: 1)
      cards.map do |name, locale|
        browser.go_to("#{base_url}/card?#{{ name:, locale: }.to_query}")
        browser.network.wait_for_idle
        path = DIRECTORY.join(self.class.filename(name, locale))
        browser.screenshot(path: path.to_s, format: "jpeg", quality: 88)
        path
      end
    ensure
      browser&.quit
    end
  end

  private
    def cards
      CARDS.keys.flat_map do |name|
        locales = self.class.localized?(name) ? I18n.available_locales : [ I18n.default_locale ]
        locales.map { |locale| [ name, locale ] }
      end
    end

    def serving
      server = Puma::Server.new(rack_app)
      port = server.add_tcp_listener("127.0.0.1", 0).addr[1]
      server.run
      yield "http://127.0.0.1:#{port}"
    ensure
      server&.stop(true)
    end

    def rack_app
      card = ->(env) { [ 200, { "content-type" => "text/html; charset=utf-8" }, [ render_card(Rack::Request.new(env).params) ] ] }
      stylesheet = ->(_env) { [ 200, { "content-type" => "text/css" }, [ STYLESHEET.read ] ] }

      Rack::Builder.app do
        map("/card") { run card }
        map("/card.css") { run stylesheet }
        run Rails.application
      end
    end

    def render_card(params)
      I18n.with_locale(params.fetch("locale")) do
        ApplicationController.renderer.render(inline: TEMPLATE.read, locals: { card: CARDS.fetch(params.fetch("name")) })
      end
    end
end
