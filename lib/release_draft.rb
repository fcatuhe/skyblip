require_relative "firmware_changes"
require_relative "gh"

class ReleaseDraft
  # INFO: fc 25nov26 the briefing shows pilots the two news sections under these exact headings (server/app/models/release/notes.rb)
  NEWS = {
    "What's new" => "For pilots, in English: one \"- \" line per change they will notice. Delete the firmware pull requests below that they will not, and rewrite the rest for them.",
    "Nouveautés" => "Pour les pilotes, en français : une ligne « - » par changement qu'ils remarqueront. Supprimez les pull requests ci-dessous qu'ils ne remarqueront pas, et traduisez le reste pour eux."
  }.freeze

  attr_reader :tag, :repository

  def initialize(tag:, repository:)
    @tag = tag
    @repository = repository
  end

  def create(files)
    previous = previous_tag
    puts Gh.run("release", "create", tag, *Dir.children(files).sort.map { |file| File.join(files, file) },
                "--verify-tag", "--draft", "--notes", news(previous), "--generate-notes", *([ "--notes-start-tag", previous ] if previous))
  end

  private
    def previous_tag
      Gh.run("release", "list", "-L", "100", "--exclude-drafts", "--exclude-pre-releases", "--json", "tagName", "--jq", ".[].tagName")
        .lines(chomp: true).find { |candidate| candidate.match?(/\Av\d/) }
    end

    def news(previous)
      changes = FirmwareChanges.new(repository:, tag:, previous:).titles.map { |title| "- #{title}" }
      NEWS.map { |heading, guide| [ "## #{heading}", "<!-- #{guide} -->", (changes.join("\n") if changes.any?) ].compact.join("\n\n") }.join("\n\n")
    end
end
