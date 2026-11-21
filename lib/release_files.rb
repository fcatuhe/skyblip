require "digest"
require "fileutils"
require_relative "radio_protocols"
require_relative "release_notice"

class ReleaseFiles
  IMAGE_ENDINGS = %w[.uf2 .signed.bin].freeze
  HUB_IMAGE_ENDING = ".full.signed.bin".freeze
  BUILD_ENDINGS = %w[.version .signed.confirmed.bin].freeze
  SIMULATOR_FILES = %w[skyblip_simulator.js skyblip_simulator.wasm embed.js scenarios.json].freeze

  attr_reader :slugs, :version

  def initialize(images, products: nil)
    @images = images
    records = Dir.glob(File.join(images, "*.version")).sort.to_h { |record| File.read(record).split.first(2) }
    raise ArgumentError, "No product image in #{images}" if records.empty?

    if records.values.uniq.size > 1
      raise ArgumentError, "The products disagree on the version: #{records.map { |slug, version| "#{slug} #{version}" }.join(", ")}"
    end
    @slugs = records.keys
    @version = records.values.first
    refuse_other_products(products) if products
    refuse_missing_and_unexpected_files
  end

  def hub_image?(slug) = File.exist?(File.join(@images, "#{slug}#{HUB_IMAGE_ENDING}"))

  def stage(into:, name:, simulator:, source:, protocols: false)
    FileUtils.mkdir_p(into)
    slugs.each do |slug|
      endings = IMAGE_ENDINGS + (hub_image?(slug) ? [ HUB_IMAGE_ENDING ] : [])
      endings.each { |ending| FileUtils.cp(File.join(@images, "#{slug}#{ending}"), File.join(into, "#{slug}-#{name}#{ending}")) }
    end
    FileUtils.cp(SIMULATOR_FILES.map { |file| File.join(simulator, file) }, into)
    File.write(File.join(into, "NOTICE.md"), ReleaseNotice.new(source).to_s)
    if protocols
      notes = RadioProtocols.new(File.join(source, "firmware")).notes(version: name, products: slugs.map { |slug| slug.tr("-", "_") })
      File.write(File.join(into, "PROTOCOLS.md"), notes)
    end
    File.write(File.join(into, "SHA256SUMS"), checksums(into))
  end

  private
    def refuse_other_products(products)
      asked = products.map { |product| product.tr("_", "-") }.sort
      raise ArgumentError, "The images are of #{slugs.join(", ")}, where the products ask for #{asked.join(", ")}" unless slugs == asked
    end

    def refuse_missing_and_unexpected_files
      present = Dir.children(@images)
      missing = slugs.product(IMAGE_ENDINGS).map(&:join) - present
      raise ArgumentError, "Missing from #{@images}: #{missing.join(", ")}" if missing.any?

      unexpected = present - slugs.product(IMAGE_ENDINGS + [ HUB_IMAGE_ENDING ] + BUILD_ENDINGS).map(&:join)
      raise ArgumentError, "Unexpected files in #{@images}: #{unexpected.sort.join(", ")}" if unexpected.any?
    end

    def checksums(directory)
      Dir.children(directory).sort.map { |file| "#{Digest::SHA256.file(File.join(directory, file)).hexdigest}  #{file}\n" }.join
    end
end
