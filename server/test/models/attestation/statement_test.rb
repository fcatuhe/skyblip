require "test_helper"

class Attestation::StatementTest < ActiveSupport::TestCase
  self.use_transactional_tests = false

  setup do
    @unit = Unit.create!(device_id: vectors["device_id"], serial: 9)
    @key = @unit.keys.create!(public_key: vectors["unit_public_key"], status: :active)
    @challenge = Attestation::Challenge.create!
  end

  teardown do
    Unit.destroy_all
    Attestation::Challenge.delete_all
    Event.delete_all
  end

  # An admin revokes a key believed leaked while a statement it signed is being checked.
  test "a key revoked while its statement is checked lands before or after the attestation, never between" do
    racing, paused, landed, held = Queue.new, Queue.new, [], false

    admin = Thread.new do
      racing.pop
      ActiveRecord::Base.connection_pool.with_connection { Unit::Key.find(@key.id).revoke }
    ensure
      paused << :done
    end

    hold_statement_after_key_read_until_revocation_blocks_or_ends = Object.new.tap do |listener|
      listener.define_singleton_method(:start) do |_name, _id, payload|
        paused << :waiting if Thread.current == admin && payload[:sql].start_with?("BEGIN")
      end
      listener.define_singleton_method(:finish) do |_name, _id, payload|
        if Thread.current == admin && payload[:sql].start_with?(%(UPDATE "unit_keys"))
          landed << :revoked
        elsif Thread.current != admin && payload[:sql].start_with?(%(INSERT INTO "unit_firmwares"))
          landed << :attested
        elsif Thread.current != admin && !held && payload[:sql].start_with?(%(SELECT "unit_keys"))
          held = true
          racing << :go
          paused.pop
        end
      end
    end

    ActiveSupport::Notifications.subscribed(hold_statement_after_key_read_until_revocation_blocks_or_ends, "sql.active_record") do
      Attestation::Statement.new(signed_statement(@challenge)).record
      admin.join
    end

    assert @key.reload.revoked?
    assert_equal [ :attested, :revoked ], landed
  end
end
