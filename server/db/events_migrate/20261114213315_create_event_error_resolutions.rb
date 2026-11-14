class CreateEventErrorResolutions < ActiveRecord::Migration[8.1]
  def change
    create_table :event_error_resolutions do |t|
      t.string :fingerprint, null: false, index: { unique: true }
      t.datetime :resolved_at, null: false

      t.timestamps
    end
  end
end
