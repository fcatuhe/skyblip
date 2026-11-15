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

ActiveRecord::Schema[8.1].define(version: 2026_11_14_185547) do
  create_table "captures", force: :cascade do |t|
    t.string "device", null: false
    t.string "browser"
    t.string "profile", null: false
    t.integer "session", null: false
    t.integer "records", null: false
    t.integer "bytes", null: false
    t.string "firmware"
    t.binary "body", null: false
    t.datetime "kept_until", null: false
    t.datetime "created_at", null: false
    t.datetime "updated_at", null: false
    t.index ["browser"], name: "index_captures_on_browser"
    t.index ["device", "created_at"], name: "index_captures_on_device_and_created_at"
    t.index ["kept_until"], name: "index_captures_on_kept_until"
  end
end
