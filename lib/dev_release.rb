require "digest"
require "fileutils"
require "open3"
require "tmpdir"
require_relative "radio_protocols"

class DevRelease
  KEEP_MAIN = 3
  IMAGE_ENDINGS = %w[.uf2 .signed.bin].freeze
  HUB_IMAGE_ENDING = ".full.signed.bin".freeze
  SIMULATOR_FILES = %w[skyblip_simulator.js skyblip_simulator.wasm embed.js scenarios.json].freeze
  BUILD_ENDINGS = %w[.version .signed.confirmed.bin].freeze

  def self.publish_all(images:, simulator:, source:, ref:, sha:, repository:)
    release = new(**built(images), ref:)
    release.publish(images:, simulator:, source:, sha:, repository:)
    release.prune
  end

  def self.built(images)
    records = Dir.glob(File.join(images, "*.version")).sort.to_h { |record| File.read(record).split.first(2) }
    raise ArgumentError, "No product image in #{images}" if records.empty?

    if records.values.uniq.size > 1
      raise ArgumentError, "The products disagree on the version: #{records.map { |slug, version| "#{slug} #{version}" }.join(", ")}"
    end
    expected = records.keys.product(IMAGE_ENDINGS + [ HUB_IMAGE_ENDING ] + BUILD_ENDINGS).map(&:join)
    unexpected = Dir.children(images) - expected
    raise ArgumentError, "Unexpected files in #{images}: #{unexpected.sort.join(", ")}" if unexpected.any?

    { slugs: records.keys, version: records.values.first }
  end
  private_class_method :built

  def self.branch_label(ref) = ref.b.gsub(/[^A-Za-z0-9.]/n, "-").force_encoding(Encoding::UTF_8)

  attr_reader :slugs, :version, :ref

  def initialize(slugs:, version:, ref:)
    @slugs = slugs
    @version = version
    @ref = ref
  end

  def publish(images:, simulator:, source:, sha:, repository:)
    if released?
      puts "#{tag} is already released, nothing to do"
    else
      Dir.mktmpdir do |release|
        stage(images:, simulator:, source:, into: release)
        previous = previous_main_tag(release_tags) if main?
        puts gh("release", "create", tag, *Dir.children(release).sort.map { |file| File.join(release, file) },
                "--target", sha, "--prerelease", "--latest=false", "--title", title,
                "--notes", notes(images:, sha:, repository:, source:),
                *([ "--generate-notes", "--notes-start-tag", previous ] if previous))
      end
    end
  end

  def prune
    stale_tags(release_tags, open_branches).each do |stale|
      puts "deleting #{stale}"
      gh "release", "delete", stale, "--cleanup-tag", "--yes"
    end
  end

  private
    def tag = main? ? "dev-#{build}" : "dev-#{build}-#{self.class.branch_label(ref)}"
    def title = main? ? "skyBlip dev #{version}" : "skyBlip dev #{version} (#{ref})"

    def notes(images:, sha:, repository:, source:)
      [
        "skyBlip #{version} from #{ref}, built from #{sha} and signed with the development key.",
        *slugs.map { |slug| product_notes(slug, images:, repository:, source:) }
      ].join("\n\n")
    end

    def product_notes(slug, images:, repository:, source:)
      [
        "## #{product_name(slug)}",
        "",
        "Radio protocols: #{RadioProtocols.new(File.join(source, "firmware")).of(slug.tr("-", "_"))}.",
        "",
        "- `.signed.bin` installs over Bluetooth only on a unit whose bootloader trusts that key ([firmware/keys](https://github.com/#{repository}/tree/main/firmware/keys)), older builds included.",
        ("- `.full.signed.bin` is the same image with the sensor hub's firmware, for a unit the update page asks it for." if File.exist?(hub_image(images, slug))),
        "- `.uf2` installs on any unit by drag-and-drop, and moves it to the development key."
      ].compact.join("\n")
    end

    def stage(images:, simulator:, source:, into:)
      FileUtils.mkdir_p(into)
      slugs.each do |slug|
        endings = IMAGE_ENDINGS + (File.exist?(hub_image(images, slug)) ? [ HUB_IMAGE_ENDING ] : [])
        endings.each { |ending| FileUtils.cp(File.join(images, "#{slug}#{ending}"), File.join(into, "#{slug}-#{tag}#{ending}")) }
      end
      FileUtils.cp(SIMULATOR_FILES.map { |file| File.join(simulator, file) }, into)
      FileUtils.cp(notices(source), into)
      File.write(File.join(into, "SHA256SUMS"), checksums(into))
    end

    def previous_main_tag(tags) = own_tags(tags).grep(/\Adev-\d+\z/).max_by { |main| main.delete_prefix("dev-").to_i }

    def stale_tags(tags, open_branches)
      own = own_tags(tags)
      stale_main = own.grep(/\Adev-\d+\z/).sort_by { |main| -main.delete_prefix("dev-").to_i }.drop(KEEP_MAIN)
      stale_main + stale_branch_tags(own, open_branches)
    end

    def main? = ref == "main"
    def build = version.split("+", 2).fetch(1)
    def hub_image(images, slug) = File.join(images, "#{slug}#{HUB_IMAGE_ENDING}")
    def product_name(slug) = "skyBlip #{slug.delete_prefix("skyblip-").capitalize}"

    def own_tags(tags) = tags.grep(/\Adev-/)

    def stale_branch_tags(own, open_branches)
      kept = Hash.new(0)
      own.filter_map { |candidate| candidate.match(/\Adev-(\d+)-(.+)\z/) }
        .sort_by { |match| [ match[2], -match[1].to_i ] }
        .filter_map do |match|
          branch = match[2]
          if match[0] == tag
            kept[branch] = 1
            nil
          elsif !open_branches.include?(branch) || (kept[branch] += 1) > 1
            match[0]
          end
        end
    end

    def notices(source)
      licenses = Dir.glob(File.join(source, "LICENSES", "*.txt")).sort
      raise ArgumentError, "No license texts in #{source}/LICENSES" if licenses.empty?

      [ File.join(source, "LICENSE"), File.join(source, "NOTICE.md"), *licenses ]
    end

    def checksums(directory)
      Dir.children(directory).sort.map { |file| "#{Digest::SHA256.file(File.join(directory, file)).hexdigest}  #{file}\n" }.join
    end

    def released? = system("gh", "release", "view", tag, out: File::NULL, err: File::NULL)
    def release_tags = gh("release", "list", "-L", "100", "--json", "tagName", "--jq", ".[].tagName").lines(chomp: true)

    def open_branches
      gh("pr", "list", "--state", "open", "-L", "1000", "--json", "headRefName", "--jq", ".[].headRefName").lines(chomp: true).map { |branch| self.class.branch_label(branch) }
    end

    def gh(*arguments)
      output, status = Open3.capture2("gh", *arguments)
      raise "gh #{arguments.first(2).join(" ")} failed: #{status}" unless status.success?

      output
    end
end
