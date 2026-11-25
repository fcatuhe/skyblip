require "test_helper"

class OAuth::RemoteTest < ActiveSupport::TestCase
  KEY = OpenSSL::PKey::EC.generate("prime256v1")
  CERTIFICATE = OpenSSL::X509::Certificate.new.tap do |certificate|
    certificate.version = 2
    certificate.serial = 1
    certificate.subject = certificate.issuer = OpenSSL::X509::Name.parse("/CN=127.0.0.1")
    certificate.public_key = KEY
    certificate.not_before = 1.minute.ago
    certificate.not_after = 1.day.from_now
    certificate.add_extension(OpenSSL::X509::ExtensionFactory.new.create_extension("subjectAltName", "IP:127.0.0.1"))
    certificate.sign(KEY, "SHA256")
  end
  # INFO: fc 25nov26 Net::HTTP trusts only the default store, so the test adds this throwaway certificate to it
  OpenSSL::SSL::SSLContext::DEFAULT_CERT_STORE.add_cert(CERTIFICATE)

  test "a peer that closes the connection without answering is unreadable" do
    assert_unreadable ""
  end

  test "a body that claims gzip and is not is unreadable" do
    assert_unreadable "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 4\r\n\r\nnope"
  end

  test "a Content-Length that is no number is unreadable" do
    assert_unreadable "HTTP/1.1 200 OK\r\nContent-Length: many\r\n\r\n{}"
  end

  private
    def assert_unreadable(answer)
      answering(answer) do |uri|
        assert_raises(OAuth::Remote::Unreadable) { OAuth::Remote.json(uri) }
      end
    end

    def answering(answer)
      context = OpenSSL::SSL::SSLContext.new.tap { |context| context.key, context.cert = KEY, CERTIFICATE }
      server = OpenSSL::SSL::SSLServer.new(TCPServer.new("127.0.0.1", 0), context)
      peer = Thread.new do
        loop do
          socket = server.accept
          socket.gets("\r\n\r\n")
          socket.write(answer)
          socket.close
        end
      end

      yield URI("https://127.0.0.1:#{server.to_io.local_address.ip_port}/client.json")
    ensure
      peer&.kill
      server&.close
    end
end
