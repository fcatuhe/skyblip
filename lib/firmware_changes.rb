require_relative "gh"

class FirmwareChanges
  PATHS = %w[firmware/ scripts/build_image.sh scripts/mkuf2.py].freeze
  HEADING = "## What's Changed".freeze
  PULL_REQUEST = %r{\A\* (?<title>.+) by @\S+ in https://github\.com/\S+/pull/(?<number>\d+)\z}

  def initialize(repository:, tag:, previous:, sha: nil)
    @repository = repository
    @tag = tag
    @previous = previous
    @sha = sha
  end

  def pull_requests
    @pull_requests ||= pull_request_lines(generated_notes).select { |line| touches_firmware?(line[PULL_REQUEST, :number]) }
  end

  def titles = pull_requests.map { |line| line[PULL_REQUEST, :title] }

  private
    attr_reader :repository, :tag, :previous, :sha

    def generated_notes
      fields = [ "tag_name=#{tag}", ("target_commitish=#{sha}" if sha), ("previous_tag_name=#{previous}" if previous) ].compact
      Gh.run("api", "repos/#{repository}/releases/generate-notes", *fields.flat_map { |field| [ "-f", field ] }, "--jq", ".body")
    end

    def pull_request_lines(generated)
      generated.lines(chomp: true).drop_while { |line| line != HEADING }.drop(1)
        .take_while { |line| line.empty? || line.start_with?("* ") }
        .grep(PULL_REQUEST)
    end

    def touches_firmware?(number)
      files = Gh.run("api", "--paginate", "repos/#{repository}/pulls/#{number}/files", "--jq", ".[].filename").lines(chomp: true)
      files.any? { |file| file.start_with?(*PATHS) }
    end
end
