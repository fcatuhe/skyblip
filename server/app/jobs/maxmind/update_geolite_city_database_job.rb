module Maxmind
  class UpdateGeoliteCityDatabaseJob < ApplicationJob
    queue_as :default
    retry_on Net::OpenTimeout, Net::ReadTimeout, SocketError,
             wait: 5.minutes, attempts: 3

    def perform
      Maxmind::GeoliteCityDatabase.update_now
    end
  end
end
