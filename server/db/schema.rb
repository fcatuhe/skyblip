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

ActiveRecord::Schema[8.1].define(version: 2026_10_18_135637) do
  create_table "activation_challenges", force: :cascade do |t|
    t.string "server_nonce", null: false
    t.string "unit_nonce", null: false
    t.string "server_key", null: false
    t.string "ip_address"
    t.datetime "created_at", null: false
    t.datetime "updated_at", null: false
    t.index ["created_at"], name: "index_activation_challenges_on_created_at"
    t.index ["server_nonce"], name: "index_activation_challenges_on_server_nonce", unique: true
  end

  create_table "sessions", force: :cascade do |t|
    t.integer "user_id", null: false
    t.string "ip_address"
    t.string "user_agent"
    t.datetime "created_at", null: false
    t.datetime "updated_at", null: false
    t.index ["user_id"], name: "index_sessions_on_user_id"
  end

  create_table "unit_keys", force: :cascade do |t|
    t.integer "unit_id", null: false
    t.string "public_key", null: false
    t.string "status", null: false
    t.string "ip_address"
    t.string "version"
    t.string "server_key"
    t.datetime "created_at", null: false
    t.datetime "updated_at", null: false
    t.boolean "release"
    t.index ["unit_id", "public_key"], name: "index_unit_keys_on_unit_id_and_public_key", unique: true
    t.index ["unit_id"], name: "index_unit_keys_on_unit_id_active", unique: true, where: "status = 'active'"
  end

  create_table "units", force: :cascade do |t|
    t.string "device_id", null: false
    t.integer "serial", null: false
    t.datetime "created_at", null: false
    t.datetime "updated_at", null: false
    t.index ["device_id"], name: "index_units_on_device_id", unique: true
    t.index ["serial"], name: "index_units_on_serial", unique: true
    t.check_constraint "serial BETWEEN 0 AND 65535", name: "units_serial_in_sixteen_bits"
  end

  create_table "users", force: :cascade do |t|
    t.string "first_name", null: false
    t.string "last_name", null: false
    t.string "email", null: false
    t.string "password_digest", null: false
    t.integer "role", default: 0, null: false
    t.datetime "created_at", null: false
    t.datetime "updated_at", null: false
    t.index ["email"], name: "index_users_on_email", unique: true
  end

  add_foreign_key "sessions", "users"
  add_foreign_key "unit_keys", "units"
end
