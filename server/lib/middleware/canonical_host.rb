class CanonicalHost
  def initialize(app, public_url)
    @app = app
    @public_url = public_url
    @www_host = "www.#{URI(public_url).host}"
  end

  def call(env)
    request = Rack::Request.new(env)

    if request.host == @www_host
      [ 301, { "location" => "#{@public_url}#{request.fullpath}" }, [] ]
    else
      @app.call(env)
    end
  end
end
