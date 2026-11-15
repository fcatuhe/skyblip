module Caching
  def with_memory_cache
    held, Rails.cache = Rails.cache, ActiveSupport::Cache::MemoryStore.new
    yield
  ensure
    Rails.cache = held
  end
end
