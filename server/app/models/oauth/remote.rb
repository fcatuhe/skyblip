require "net/http"

module OAuth::Remote
  TIMEOUT = 5
  MAX_BYTES = 16.kilobytes
  UNREADABLE = [ Timeout::Error, SocketError, SystemCallError, OpenSSL::SSL::SSLError, Net::HTTPBadResponse ].freeze

  class Unreadable < StandardError; end

  class << self
    def json(uri)
      JSON.parse(read(uri))
    rescue JSON::ParserError => error
      raise Unreadable, "skyBlip could not read #{uri}: #{error.message}"
    end

    private
      def read(uri)
        Net::HTTP.start(uri.host, uri.port, use_ssl: true, open_timeout: TIMEOUT, read_timeout: TIMEOUT) do |http|
          http.request(Net::HTTP::Get.new(uri, "Accept" => "application/json")) do |response|
            if response.is_a?(Net::HTTPOK)
              return bounded(response, uri)
            else
              raise Unreadable, "#{uri} answered #{response.code}"
            end
          end
        end
      rescue *UNREADABLE => error
        raise Unreadable, "skyBlip could not read #{uri}: #{error.message}"
      end

      def bounded(response, uri)
        body = +""
        response.read_body do |chunk|
          body << chunk
          raise Unreadable, "#{uri} is larger than the #{MAX_BYTES / 1.kilobyte} KB skyBlip reads" if body.bytesize > MAX_BYTES
        end
        body
      end
  end
end
