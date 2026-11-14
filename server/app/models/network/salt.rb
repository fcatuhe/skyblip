# INFO: fc 14nov26 a salt is deleted once its day is over, so a pseudonym from an earlier day leads back to no address, even for us
class Network::Salt < ApplicationRecord
  scope :expired, -> { where(day: ...Date.current) }

  def self.today
    find_by(day: Date.current) || create_or_find_by!(day: Date.current) { it.secret = SecureRandom.bytes(32) }
  end

  def digest(text) = OpenSSL::HMAC.hexdigest("SHA256", secret, text)
end
