class Event::Catalog
  Entry = Data.define(:name, :group, :says, :answers, :from, :keys) do
    def browser? = from == "browser"
  end

  ENTRIES = YAML.load_file(Rails.root.join("app/models/event/catalog.yml"))
    .map { |name, entry| Entry.new(name:, from: "server", keys: [], **entry.symbolize_keys) }.index_by(&:name).freeze

  class << self
    def entries = ENTRIES.values

    def [](name) = ENTRIES[name]

    def names = ENTRIES.keys

    def browser(name) = ENTRIES[name]&.then { it if it.browser? }

    def prefix?(start) = start.present? && names.any? { it.start_with?(start) }

    def groups = entries.group_by(&:group)
  end
end
