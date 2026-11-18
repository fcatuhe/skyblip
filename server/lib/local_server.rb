class LocalServer
  def self.serving(app)
    server = Puma::Server.new(app)
    port = server.add_tcp_listener("127.0.0.1", 0).addr[1]
    server.run
    yield "http://127.0.0.1:#{port}"
  ensure
    server&.stop(true)
  end
end
