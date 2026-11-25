require "minitest/autorun"
require "fileutils"
require "json"
require "open3"
require "rbconfig"
require "tmpdir"

class ReleaseDraftTest < Minitest::Test
  ROOT = File.expand_path("../..", __dir__)
  GENERATED = <<~NOTES.chomp
    ## What's Changed
    * fix(adsl): keep the climb rate fresh by @fcatuhe in https://github.com/fcatuhe/skyblip/pull/1
    * content: scope the pledge by @fcatuhe in https://github.com/fcatuhe/skyblip/pull/2
    * infra: build the image with mkuf2 by @dependabot[bot] in https://github.com/fcatuhe/skyblip/pull/3

    **Full Changelog**: https://github.com/fcatuhe/skyblip/compare/v0.8.0...v0.9.0
  NOTES
  PULL_REQUEST_FILES = { "1" => %w[server/app/models/unit.rb firmware/protocols/adsl/encoder.cpp], "2" => %w[server/content/pages/pledge.html.erb],
                         "3" => %w[scripts/mkuf2.py] }.freeze
  ENGLISH_GUIDE = "<!-- For pilots, in English: one \"- \" line per change they will notice. Delete the firmware pull requests below that they will not, and rewrite the rest for them. -->".freeze
  FRENCH_GUIDE = "<!-- Pour les pilotes, en français : une ligne « - » par changement qu'ils remarqueront. Supprimez les pull requests ci-dessous qu'ils ne remarqueront pas, et traduisez le reste pour eux. -->".freeze

  FAKE_GH = <<~'RUBY'.freeze
    #!/usr/bin/env ruby
    require "json"
    state = ENV.fetch("FAKE_GH_STATE")
    arguments = ARGV.map { |arg| File.file?(arg) ? File.basename(arg) : arg.dup.force_encoding(Encoding::UTF_8) }
    File.open(File.join(state, "calls"), "a") { |log| log.puts JSON.generate(arguments) }
    case ARGV.first(2)
    in [ "release", "list" ] then puts File.read(File.join(state, "tags"))
    in [ "api", "--paginate" ] then puts JSON.parse(ENV.fetch("FAKE_GH_FILES")).fetch(ARGV[2][%r{/pulls/(\d+)/files\z}, 1])
    in [ "api", _ ] then puts ENV.fetch("FAKE_GH_GENERATED"), ""
    in [ "release", "create" ] then nil
    end
  RUBY

  def test_the_draft_lists_every_firmware_pull_request_since_the_last_release_in_both_languages
    Dir.mktmpdir do |root|
      run = draft(root, tags: %w[bravo-v0.8.1 v0.8.0 v0.7.0])

      assert run.success, run.output
      assert_equal [ "api", "repos/fcatuhe/skyblip/releases/generate-notes", "-f", "tag_name=v0.9.0", "-f", "previous_tag_name=v0.8.0",
                     "--jq", ".body" ], run.generated.first
      changes = "- fix(adsl): keep the climb rate fresh\n- infra: build the image with mkuf2"
      assert_equal [ [ "release", "create", "v0.9.0", "SHA256SUMS", "skyblip-go-0.9.0.signed.bin", "--verify-tag", "--draft", "--notes",
                       "## What's new\n\n#{ENGLISH_GUIDE}\n\n#{changes}\n\n## Nouveautés\n\n#{FRENCH_GUIDE}\n\n#{changes}",
                       "--generate-notes", "--notes-start-tag", "v0.8.0" ] ], run.creates
    end
  end

  # Before one release carried every product, Bravo's were tagged bravo-v*.
  def test_a_release_after_only_product_releases_lets_github_choose_where_the_notes_start
    Dir.mktmpdir do |root|
      run = draft(root, tags: %w[bravo-v0.2.0], generated: "## What's Changed\n* content: scope the pledge by @fcatuhe in https://github.com/fcatuhe/skyblip/pull/2\n")

      assert run.success, run.output
      assert_equal [ "api", "repos/fcatuhe/skyblip/releases/generate-notes", "-f", "tag_name=v0.9.0", "--jq", ".body" ], run.generated.first
      create = run.creates.first
      assert_equal "## What's new\n\n#{ENGLISH_GUIDE}\n\n## Nouveautés\n\n#{FRENCH_GUIDE}", create[create.index("--notes") + 1]
      assert_equal "--generate-notes", create.last
    end
  end

  private
    Run = Struct.new(:success, :output, :calls) do
      def creates = calls.select { |call| call.first(2) == %w[release create] }
      def generated = calls.select { |call| call.first(2) == [ "api", "repos/fcatuhe/skyblip/releases/generate-notes" ] }
    end

    def draft(root, tags:, generated: GENERATED)
      files, state = %w[release gh].map { |name| File.join(root, name).tap { |dir| FileUtils.mkdir_p(dir) } }
      %w[skyblip-go-0.9.0.signed.bin SHA256SUMS].each { |file| File.write(File.join(files, file), file) }
      File.write(File.join(state, "tags"), tags.map { |tag| "#{tag}\n" }.join)
      File.write(File.join(state, "calls"), "")
      File.write(File.join(state, "gh"), FAKE_GH)
      File.chmod(0o755, File.join(state, "gh"))

      env = { "PATH" => "#{state}:#{ENV.fetch("PATH")}", "HOME" => ENV.fetch("HOME"), "FAKE_GH_STATE" => state, "GITHUB_REF_NAME" => "v0.9.0",
              "GH_REPO" => "fcatuhe/skyblip", "FAKE_GH_GENERATED" => generated, "FAKE_GH_FILES" => JSON.generate(PULL_REQUEST_FILES) }
      output, status = Open3.capture2e(env, RbConfig.ruby, File.join(ROOT, "scripts/release_draft.rb"), files, unsetenv_others: true)
      Run.new(status.success?, output, File.readlines(File.join(state, "calls")).map { |line| JSON.parse(line) })
    end
end
