class CreateAnalyticsVisits < ActiveRecord::Migration[8.1]
  def change
    create_table :analytics_visits do |t|
      t.integer :ahoy_visit_id, null: false, index: { unique: true }
      t.integer :user_id
      t.integer :pageviews_count, null: false, default: 0
      t.integer :visit_duration, null: false, default: 0
      t.string :entry_path
      t.string :exit_path
      t.json :page_paths
      t.datetime :last_event_at
      t.string :segment, null: false, default: "non_user"
      t.string :channel, null: false, default: "direct"
      t.string :entry_section, null: false, default: "public"

      t.timestamps
    end
  end
end
