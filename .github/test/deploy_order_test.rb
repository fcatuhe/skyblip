require "minitest/autorun"
require "fileutils"
require "open3"
require "tmpdir"
require "yaml"
require_relative "../../lib/deploy_order"

# On Nov 19 a merge whose run reached its deploy job last replaced the newer merge production ran.
class DeployOrderTest < Minitest::Test
  ROOT = File.expand_path("../..", __dir__)

  def setup
    @repository = Dir.mktmpdir
    git "init", "--quiet"
    @older = commit("older")
    @newer = commit("newer")
  end

  def teardown = FileUtils.remove_entry(@repository)

  def test_a_commit_older_than_the_one_production_runs_is_not_deployed
    refute order(@older, running: @newer).deploy?
  end

  def test_a_newer_commit_the_same_one_again_or_an_empty_server_is_deployed
    assert order(@newer, running: @older).deploy?
    assert order(@newer, running: @newer).deploy?
    assert order(@newer, running: "").deploy?
  end

  def test_a_version_git_cannot_place_stops_the_deploy
    assert_raises(DeployOrder::Unknown) { order(@newer, running: "f" * 40).deploy? }
    assert_raises(DeployOrder::Unknown) { order(@newer, running: "#{@older}_uncommitted_1a2b").deploy? }
  end

  def test_the_workflow_asks_the_order_with_the_whole_history_before_it_deploys
    steps = YAML.load_file(File.join(ROOT, ".github/workflows/server.yml")).fetch("jobs").fetch("deploy").fetch("steps")
    checkout = steps.find { it["uses"]&.start_with?("actions/checkout@") }
    order = steps.index { it["id"] == "order" }
    deploy = steps.index { it["run"] == "bundle exec kamal deploy" }

    assert_equal 0, checkout.fetch("with").fetch("fetch-depth")
    assert_includes steps[order].fetch("run"), "ruby ../scripts/deploy_order.rb"
    assert_operator order, :<, deploy
    assert_equal "steps.order.outputs.deploy == 'true'", steps[deploy].fetch("if")
  end

  private
    def order(commit, running:) = DeployOrder.new(commit:, running:, repository: @repository)

    def commit(message)
      git "-c", "user.name=test", "-c", "user.email=test@example.com", "commit", "--quiet", "--allow-empty", "-m", message
      git("rev-parse", "HEAD").strip
    end

    def git(*args)
      output, status = Open3.capture2e("git", "-C", @repository, *args)
      raise output unless status.success?

      output
    end
end
