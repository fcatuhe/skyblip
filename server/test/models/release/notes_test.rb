require "test_helper"

class Release::NotesTest < ActiveSupport::TestCase
  NOTES = <<~MARKDOWN.freeze
    ## What's new

    <!-- For pilots, in English. -->
    - The alarm warbles across 4 kHz.
    * Traffic is drawn over the readings.

    ## Nouveautés

    - L'alarme module autour de 4 kHz.

    ## What's Changed
    * feat(alarm): warble the advisory by @fcatuhe in https://github.com/fcatuhe/skyblip/pull/130
    * a line by hand

    **Full Changelog**: https://github.com/fcatuhe/skyblip/compare/v0.1.0...v0.2.0
  MARKDOWN

  test "the news is read in the pilot's language, under its own heading" do
    notes = Release::Notes.new(NOTES)

    assert_equal [ "The alarm warbles across 4 kHz.", "Traffic is drawn over the readings." ], notes.news(:en).map(&:text)
    assert_equal [ "L'alarme module autour de 4 kHz." ], notes.news("fr").map(&:text)
    assert_equal [ nil ], notes.news(:fr).map(&:url).uniq
  end

  test "news not written in French falls back to the English, and notes left as the draft's comments say nothing" do
    english_only = Release::Notes.new("## What's new\n\n- The alarm warbles.\n\n## Nouveautés\n\n<!-- - Pour les pilotes. -->\n")

    assert_equal [ "The alarm warbles." ], english_only.news(:fr).map(&:text)
    assert_empty Release::Notes.new("## What's new\r\n\r\n<!--\r\n- For pilots.\r\n-->\r\n").news(:en)
  end

  test "a stable release's draft opens on the headings the briefing reads" do
    workflow = Rails.root.join("../.github/workflows/release.yml").read

    Release::Notes::NEWS_HEADINGS.each_value { |heading| assert_includes workflow, %("## #{heading}") }
  end

  test "the technical changes link each pull request, and keep a line written by hand as it is" do
    changes = Release::Notes.new(NOTES).technical

    assert_equal [ "feat(alarm): warble the advisory", "a line by hand" ], changes.map(&:text)
    assert_equal [ "https://github.com/fcatuhe/skyblip/pull/130", nil ], changes.map(&:url)
  end

  test "a pull request link outside the repository is kept as text" do
    change = Release::Notes.new("## What's Changed\n* fix: x by @a in https://evil.example/fcatuhe/skyblip/pull/1\n").technical.first

    assert_nil change.url
    assert_equal "fix: x by @a in https://evil.example/fcatuhe/skyblip/pull/1", change.text
  end
end
