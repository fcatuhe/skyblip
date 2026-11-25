require "tmpdir"
require_relative "firmware_changes"
require_relative "gh"
require_relative "radio_protocols"
require_relative "release_files"

class DevRelease
  KEEP_MAIN = 3

  def self.publish_all(images:, simulator:, source:, ref:, sha:, repository:)
    release = new(files: ReleaseFiles.new(images), ref:)
    release.publish(simulator:, source:, sha:, repository:)
    release.prune
  end

  def self.branch_label(ref) = ref.b.gsub(/[^A-Za-z0-9.]/n, "-").force_encoding(Encoding::UTF_8)

  attr_reader :files, :ref

  def initialize(files:, ref:)
    @files = files
    @ref = ref
  end

  def publish(simulator:, source:, sha:, repository:)
    if released?
      puts "#{tag} is already released, nothing to do"
    else
      Dir.mktmpdir do |release|
        files.stage(into: release, name: tag, simulator:, source:)
        previous = previous_main_tag(release_tags) if main?
        changes = changes_since(previous, sha:, repository:) if previous
        puts gh("release", "create", tag, *Dir.children(release).sort.map { |file| File.join(release, file) },
                "--target", sha, "--prerelease", "--latest=false", "--title", title,
                "--notes", notes(sha:, repository:, source:, changes:))
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

    def notes(sha:, repository:, source:, changes:)
      [
        "skyBlip #{version} from #{ref}, built from #{sha} and signed with the development key.",
        changes,
        *files.slugs.map { |slug| product_notes(slug, repository:, source:) }
      ].compact.join("\n\n")
    end

    def product_notes(slug, repository:, source:)
      [
        "## #{product_name(slug)}",
        "",
        "Radio protocols: #{RadioProtocols.new(File.join(source, "firmware")).of(slug.tr("-", "_"))}.",
        "",
        "- `.signed.bin` installs over Bluetooth only on a unit whose bootloader trusts that key ([firmware/keys](https://github.com/#{repository}/tree/main/firmware/keys)), older builds included.",
        ("- `.full.signed.bin` is the same image with the sensor hub's firmware, for a unit the update page asks it for." if files.hub_image?(slug)),
        "- `.uf2` installs on any unit by drag-and-drop, and moves it to the development key."
      ].compact.join("\n")
    end

    # INFO: fc 15nov26 gh appends --generate-notes below --notes, so the notes are generated apart to put the changes above the products
    def changes_since(previous, sha:, repository:)
      firmware = FirmwareChanges.new(repository:, tag:, previous:, sha:).pull_requests
      [
        ([ FirmwareChanges::HEADING, *firmware ].join("\n") if firmware.any?),
        "**Full Changelog**: https://github.com/#{repository}/compare/#{previous}...#{tag}"
      ].compact.join("\n\n")
    end

    def previous_main_tag(tags) = own_tags(tags).grep(/\Adev-\d+\z/).max_by { |main| main.delete_prefix("dev-").to_i }

    def stale_tags(tags, open_branches)
      own = own_tags(tags)
      stale_main = own.grep(/\Adev-\d+\z/).sort_by { |main| -main.delete_prefix("dev-").to_i }.drop(KEEP_MAIN)
      stale_main + stale_branch_tags(own, open_branches)
    end

    def main? = ref == "main"
    def version = files.version
    def build = version.split("+", 2).fetch(1)
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

    def released? = system("gh", "release", "view", tag, out: File::NULL, err: File::NULL)
    def release_tags = gh("release", "list", "-L", "100", "--json", "tagName", "--jq", ".[].tagName").lines(chomp: true)

    def open_branches
      gh("pr", "list", "--state", "open", "-L", "1000", "--json", "headRefName", "--jq", ".[].headRefName").lines(chomp: true).map { |branch| self.class.branch_label(branch) }
    end

    def gh(*arguments) = Gh.run(*arguments)
end
