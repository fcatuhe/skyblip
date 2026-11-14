# This file is auto-generated from the current state of the database. Instead
# of editing this file, please use the migrations feature of Active Record to
# incrementally modify your database, and then regenerate this schema definition.
#
# This file is the source Rails uses to define your schema when running `bin/rails
# db:schema:load`. When creating a new database, `bin/rails db:schema:load` tends to
# be faster and is potentially less error prone than running all of your
# migrations from scratch. Old migrations may fail to apply correctly if those
# migrations use external dependencies or application code.
#
# It's strongly recommended that you check this file into your version control system.

ActiveRecord::Schema[8.1].define(version: 2026_11_14_121300) do
  create_table "events", force: :cascade do |t|
    t.string "name", null: false
    t.json "payload", default: {}, null: false
    t.json "context", default: {}, null: false
    t.string "source"
    t.string "uuid"
    t.integer "user_id"
    t.string "request_id"
    t.virtual "device", type: :string, as: "coalesce(json_extract(payload, '$.device'), json_extract(context, '$.device'))", stored: false
    t.virtual "browser", type: :string, as: "json_extract(context, '$.browser')", stored: false
    t.virtual "fingerprint", type: :string, as: "json_extract(payload, '$.fingerprint')", stored: false
    t.datetime "occurred_at", null: false
    t.index ["browser", "occurred_at"], name: "index_events_on_browser_and_occurred_at"
    t.index ["device", "occurred_at"], name: "index_events_on_device_and_occurred_at"
    t.index ["fingerprint", "occurred_at"], name: "index_events_on_fingerprint_and_occurred_at"
    t.index ["name", "occurred_at"], name: "index_events_on_name_and_occurred_at"
    t.index ["occurred_at"], name: "index_events_on_occurred_at"
    t.index ["request_id"], name: "index_events_on_request_id"
    t.index ["user_id", "occurred_at"], name: "index_events_on_user_id_and_occurred_at"
    t.index ["uuid"], name: "index_events_on_uuid", unique: true
  end
end
