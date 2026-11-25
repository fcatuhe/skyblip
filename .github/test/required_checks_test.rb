require "minitest/autorun"
require "yaml"

# The main ruleset requires only the passed job of each workflow, so a check job it does not need merges red.
class RequiredChecksTest < Minitest::Test
  ROOT = File.expand_path("../..", __dir__)

  def test_server_passed_needs_every_check_job
    assert_gate "server", "server passed", publishing: %w[deploy]
  end

  def test_firmware_passed_needs_every_check_job
    assert_gate "firmware", "firmware passed", publishing: %w[dev-release]
  end

  private
    def assert_gate(workflow, name, publishing:)
      jobs = YAML.load_file(File.join(ROOT, ".github/workflows/#{workflow}.yml")).fetch("jobs")
      passed = jobs.fetch("passed")

      assert_equal name, passed.fetch("name")
      assert_equal "always()", passed.fetch("if")
      assert_equal (jobs.keys - publishing - %w[passed]).sort, passed.fetch("needs").sort
    end
end
