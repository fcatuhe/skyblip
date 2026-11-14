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

ActiveRecord::Schema[8.1].define(version: 2026_11_14_161715) do
  create_table "activation_challenges", force: :cascade do |t|
    t.string "server_nonce", null: false
    t.string "unit_nonce", null: false
    t.string "server_key_id", null: false
    t.string "ip_address"
    t.datetime "created_at", null: false
    t.datetime "updated_at", null: false
    t.index ["created_at"], name: "index_activation_challenges_on_created_at"
    t.index ["server_nonce"], name: "index_activation_challenges_on_server_nonce", unique: true
  end

  create_table "attestation_challenges", force: :cascade do |t|
    t.string "server_nonce", null: false
    t.datetime "created_at", null: false
    t.datetime "updated_at", null: false
    t.index ["created_at"], name: "index_attestation_challenges_on_created_at"
    t.index ["server_nonce"], name: "index_attestation_challenges_on_server_nonce", unique: true
  end

  create_table "network_salts", force: :cascade do |t|
    t.date "day", null: false
    t.binary "secret", null: false
    t.datetime "created_at", null: false
    t.datetime "updated_at", null: false
    t.index ["day"], name: "index_network_salts_on_day", unique: true
  end

  create_table "oauth_clients", force: :cascade do |t|
    t.string "document_url", null: false
    t.string "name", null: false
    t.string "client_uri"
    t.json "redirect_uris", null: false
    t.datetime "fetched_at", null: false
    t.datetime "created_at", null: false
    t.datetime "updated_at", null: false
    t.index ["document_url"], name: "index_oauth_clients_on_document_url", unique: true
  end

  create_table "oauth_codes", force: :cascade do |t|
    t.integer "grant_id", null: false
    t.string "digest", null: false
    t.string "code_challenge", null: false
    t.string "redirect_uri", null: false
    t.datetime "expires_at", null: false
    t.datetime "redeemed_at"
    t.datetime "created_at", null: false
    t.datetime "updated_at", null: false
    t.index ["digest"], name: "index_oauth_codes_on_digest", unique: true
    t.index ["expires_at"], name: "index_oauth_codes_on_expires_at"
    t.index ["grant_id"], name: "index_oauth_codes_on_grant_id"
  end

  create_table "oauth_grants", force: :cascade do |t|
    t.integer "user_id", null: false
    t.integer "client_id", null: false
    t.string "scope", null: false
    t.string "resource", null: false
    t.datetime "approved_at", null: false
    t.datetime "last_used_at"
    t.datetime "created_at", null: false
    t.datetime "updated_at", null: false
    t.index ["approved_at"], name: "index_oauth_grants_on_approved_at"
    t.index ["client_id"], name: "index_oauth_grants_on_client_id"
    t.index ["user_id", "client_id"], name: "index_oauth_grants_on_user_id_and_client_id", unique: true
  end

  create_table "oauth_tokens", force: :cascade do |t|
    t.integer "grant_id", null: false
    t.string "kind", null: false
    t.string "digest", null: false
    t.datetime "expires_at", null: false
    t.datetime "spent_at"
    t.datetime "created_at", null: false
    t.datetime "updated_at", null: false
    t.index ["digest"], name: "index_oauth_tokens_on_digest", unique: true
    t.index ["expires_at"], name: "index_oauth_tokens_on_expires_at"
    t.index ["grant_id"], name: "index_oauth_tokens_on_grant_id"
  end

  create_table "sessions", force: :cascade do |t|
    t.integer "user_id", null: false
    t.string "ip_address"
    t.string "user_agent"
    t.datetime "created_at", null: false
    t.datetime "updated_at", null: false
    t.index ["user_id"], name: "index_sessions_on_user_id"
  end

  create_table "unit_firmwares", force: :cascade do |t|
    t.integer "unit_id", null: false
    t.string "version", null: false
    t.boolean "release", null: false
    t.datetime "created_at", null: false
    t.datetime "updated_at", null: false
    t.index ["unit_id", "created_at"], name: "index_unit_firmwares_on_unit_id_and_created_at"
  end

  create_table "unit_keys", force: :cascade do |t|
    t.integer "unit_id", null: false
    t.string "public_key", null: false
    t.string "status", null: false
    t.string "ip_address"
    t.string "version"
    t.string "server_key_id"
    t.datetime "created_at", null: false
    t.datetime "updated_at", null: false
    t.boolean "release"
    t.index ["public_key"], name: "index_unit_keys_on_public_key"
    t.index ["unit_id", "public_key"], name: "index_unit_keys_on_unit_id_and_public_key", unique: true
    t.index ["unit_id"], name: "index_unit_keys_on_unit_id_active", unique: true, where: "status = 'active'"
  end

  create_table "units", force: :cascade do |t|
    t.string "device_id", null: false
    t.integer "serial", null: false
    t.datetime "created_at", null: false
    t.datetime "updated_at", null: false
    t.string "enrolled_from"
    t.string "enrollment_network_pseudonym"
    t.index ["device_id"], name: "index_units_on_device_id", unique: true
    t.index ["enrolled_from", "created_at"], name: "index_units_on_enrolled_from_and_created_at"
    t.index ["enrollment_network_pseudonym", "created_at"], name: "index_units_on_enrollment_network_pseudonym_and_created_at"
    t.index ["serial"], name: "index_units_on_serial", unique: true
    t.check_constraint "serial BETWEEN 1 AND 65535", name: "units_serial_from_one"
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

  add_foreign_key "oauth_codes", "oauth_grants", column: "grant_id", on_delete: :cascade
  add_foreign_key "oauth_grants", "oauth_clients", column: "client_id"
  add_foreign_key "oauth_grants", "users"
  add_foreign_key "oauth_tokens", "oauth_grants", column: "grant_id", on_delete: :cascade
  add_foreign_key "sessions", "users"
  add_foreign_key "unit_firmwares", "units"
  add_foreign_key "unit_keys", "units"
end
