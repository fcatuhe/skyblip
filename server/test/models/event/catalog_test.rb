require "test_helper"

class Event::CatalogTest < ActiveSupport::TestCase
  NOTIFIED = /(?:Rails\.event\.notify|record_now)[ (]"([a-z_.]+)"/
  RECORDED = /["']((?:browser|bluetooth|device|activation)\.[a-z_]+)["']/
  RAISED_ELSEWHERE = %w[Traces].freeze

  # The Events page and the agents explain every event by its entry, so an event added without one would go unexplained.
  test "every event the app emits is in the catalog, and every entry is still emitted" do
    notified = Dir[Rails.root.join("{app,config}/**/*.rb")].flat_map { File.read(it).scan(NOTIFIED).flatten }
    recorded = Dir[Rails.root.join("app/javascript/**/*.js")].flat_map { File.read(it).scan(RECORDED).flatten }
    emitted = (notified + recorded).uniq

    assert_empty emitted - Event::Catalog.names, "emitted without an entry"
    assert_empty Event::Catalog.entries.reject { it.group.in?(RAISED_ELSEWHERE) }.map(&:name) - emitted, "an entry nothing emits"
  end

  test "an event the browser sends names the keys it may carry, and each key has a type" do
    typed = Event::Journal::WORDS + Event::Journal::COUNTS + Event::Journal::FLAGS + Event::Journal::TEXTS.keys + Event::Journal::PATHS + Event::Journal::LISTS

    Event::Catalog.entries.select(&:browser?).each do |entry|
      assert_empty entry.keys - typed, "#{entry.name} carries a key with no type"
    end
  end
end
