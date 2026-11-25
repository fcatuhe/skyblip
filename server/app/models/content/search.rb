class Content::Search
  LIMIT = 10
  MAX_TERMS = 12
  EXCERPT = 400
  TERM = /[[:alnum:]]+(?:-[[:alnum:]]+)*/
  TITLE_WEIGHT = 5
  DESCRIPTION_WEIGHT = 3

  Result = Data.define(:page, :score, :excerpt)

  # INFO: fc 25nov26 folding every page takes most of a search, and a page changes only with a deploy, so each process folds it once
  DOCUMENTS = Concurrent::Map.new

  def self.fold(text) = text.to_s.unicode_normalize(:nfkd).gsub(/\p{Mn}/, "").downcase

  def initialize(query, locale:)
    @terms = self.class.fold(query).scan(TERM).uniq.first(MAX_TERMS)
    @documents = Content::Page.read_in(locale).map { |page| DOCUMENTS.compute_if_absent(page) { Document.new(page) } }
  end

  def results
    @documents.map { |document| [ document, score_of(document) ] }.select { it.last.positive? }.max_by(LIMIT, &:last)
      .map { |document, score| Result.new(page: document.page, score:, excerpt: document.excerpt(weights).truncate(EXCERPT)) }
  end

  private
    def score_of(document) = weights.sum { |term, weight| weight * Math.log(1 + frequency(document, term)) }

    # INFO: fc 25nov26 a word on every page, such as "the" or "skyBlip", weighs next to nothing, so a question ranks by its rarer words
    def weights
      @weights ||= @terms.index_with do |term|
        found_in = @documents.count { frequency(it, term).positive? }
        Math.log((@documents.size + 1.0) / (found_in + 0.5)) if found_in.positive?
      end.compact
    end

    def frequency(document, term) = (@frequencies ||= {})[[ document, term ]] ||= document.frequency(term)
end
