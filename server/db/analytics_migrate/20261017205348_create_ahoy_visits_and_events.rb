class CreateAhoyVisitsAndEvents < ActiveRecord::Migration[8.1]
  def change
    create_table :ahoy_visits do |t|
      t.string :visit_token
      t.string :visitor_token
      t.references :user

      t.string :ip
      t.text :user_agent
      t.text :referrer
      t.string :referring_domain
      t.text :landing_page

      t.string :browser
      t.string :os
      t.string :device_type

      t.string :country
      t.string :country_code
      t.string :region
      t.string :city
      t.float :latitude
      t.float :longitude

      t.datetime :started_at

      t.index :visit_token, unique: true
      t.index [ :visitor_token, :started_at ]
      t.index :started_at
    end

    create_table :ahoy_events do |t|
      t.references :visit
      t.references :user

      t.string :name
      t.text :properties
      t.datetime :time

      t.index [ :name, :time ]
    end
  end
end
