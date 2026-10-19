module Activation
  NONCE = /\A[0-9a-f]{32}\z/
  DEVICE_ID = /\A[0-9a-f]{16}\z/
  PUBLIC_KEY = /\A[0-9a-f]{64}\z/
  SIGNATURE = /\A[0-9a-f]{128}\z/
  SERVER_KEY_ID = /\A[0-9a-f]{8}\z/
  VERSION = /\A[0-9A-Za-z.+-]{1,23}\z/

  def self.table_name_prefix
    "activation_"
  end
end
