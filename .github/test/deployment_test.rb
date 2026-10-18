require "minitest/autorun"
require "erb"
require "yaml"
require "json"
require "open3"
require "tmpdir"

class DeploymentTest < Minitest::Test
  ROOT = File.expand_path("../..", __dir__)

  def test_kamal_preserves_the_clean_clone_and_server_directory
    builder = deployment.fetch("builder")

    assert_equal "arm64", builder.fetch("arch")
    refute builder.key?("context")
    refute builder.key?("dockerfile")
  end

  def test_application_and_remote_builder_share_the_crooz_host_and_debian_user
    config = deployment
    host = ENV.fetch("SKYBLIP_SERVER_IP", "crooz")

    assert_equal [ host ], config.fetch("servers").fetch("web")
    assert_equal "debian", config.fetch("ssh").fetch("user")
    assert_equal "localhost:5555", config.fetch("registry").fetch("server")
    if RUBY_PLATFORM.match?(/aarch64|arm64/)
      refute config.fetch("builder").key?("remote")
    else
      assert_equal "ssh://debian@#{host}", config.fetch("builder").fetch("remote")
    end
  end

  def test_sqlite_and_solid_queue_live_in_the_persistent_volume
    assert_equal [ "skyblip_storage:/rails/storage" ], deployment.fetch("volumes")
    assert deployment.fetch("env").fetch("clear").fetch("SOLID_QUEUE_IN_PUMA")
    assert_equal [ "RAILS_MASTER_KEY" ], deployment.fetch("env").fetch("secret")
  end

  def test_deployment_waits_for_the_gates_on_main
    workflow = YAML.load_file(File.join(ROOT, ".github/workflows/server.yml"))
    jobs = workflow.fetch("jobs")
    deploy = jobs.fetch("deploy")

    assert_equal (jobs.keys - %w[deploy]).sort, deploy.fetch("needs").sort
    assert_includes deploy.fetch("if"), "github.ref == 'refs/heads/main'"
    refute deploy.fetch("concurrency").fetch("cancel-in-progress")
    assert_equal "server_production", deploy.fetch("environment").fetch("name")
  end

  def test_development_and_production_checksum_the_simulator_before_publishing
    %w[firmware release].each do |name|
      workflow = File.read(File.join(ROOT, ".github/workflows/#{name}.yml"))
      copy = workflow.index(%r{^ +cp (?:simulator|firmware/build/browser)/skyblip_simulator\.js})
      checksums = workflow.index("sha256sum -- * > SHA256SUMS")

      assert copy, "#{name} copies simulator release assets"
      assert checksums, "#{name} generates checksums"
      assert_operator copy, :<, checksums
      assert_includes workflow, "/embed.js release/"
      scenarios = workflow.index("cp simulator/scenarios.json release/")
      assert scenarios, "#{name} includes the scenario bundle"
      assert_operator scenarios, :<, checksums
    end
    production = File.read(File.join(ROOT, ".github/workflows/release.yml"))
    assert_includes production, "subject-checksums: release/SHA256SUMS"
  end

  def test_both_simulator_artifacts_contain_the_scenario_bundle
    %w[firmware release].each do |name|
      workflow = YAML.load_file(File.join(ROOT, ".github/workflows/#{name}.yml"))
      steps = workflow.fetch("jobs").fetch("simulator").fetch("steps")
      artifact = steps.find { |step| step.dig("with", "name") == "browser-simulator" }

      assert_includes artifact.fetch("with").fetch("path").lines.map(&:strip), "firmware/build/browser/scenarios.json", name
      assert steps.any? { |step| step["run"]&.include?("ruby scripts/simulator_scenarios.rb > firmware/build/browser/scenarios.json") }, name
    end
  end

  def test_the_job_holding_the_production_key_runs_no_emscripten
    image = YAML.load_file(File.join(ROOT, ".github/workflows/release.yml")).fetch("jobs").fetch("image")

    assert_includes image.fetch("needs"), "simulator"
    refute image.fetch("steps").any? { |step| step["uses"].to_s.include?("setup-emsdk") || step["run"].to_s.include?("make -C firmware browser") }
  end

  def test_scenario_bundle_contains_the_firmware_scenarios_from_any_directory
    output, error, status = Open3.capture3(RbConfig.ruby, File.join(ROOT, "scripts/simulator_scenarios.rb"), chdir: Dir.tmpdir)
    paths = Dir.glob(File.join(ROOT, "firmware/scenarios/*.json")).sort

    assert status.success?, error
    scenarios = JSON.parse(output)
    assert_equal paths.map { |path| File.basename(path) }, scenarios.keys
    paths.each { |path| assert_equal JSON.parse(File.read(path)), scenarios.fetch(File.basename(path)) }
  end

  def test_scenario_bundle_accepts_a_source_directory_and_rejects_malformed_input
    Dir.mktmpdir do |directory|
      File.write(File.join(directory, "custom.json"), JSON.generate({ name: "custom", events: [] }))
      output, error, status = Open3.capture3(RbConfig.ruby, File.join(ROOT, "scripts/simulator_scenarios.rb"), directory)

      assert status.success?, error
      assert_equal({ "custom.json" => { "name" => "custom", "events" => [] } }, JSON.parse(output))

      File.write(File.join(directory, "broken.json"), "{")
      output, error, status = Open3.capture3(RbConfig.ruby, File.join(ROOT, "scripts/simulator_scenarios.rb"), directory)

      refute status.success?
      assert_empty output
      assert_includes error, "JSON::ParserError"
    end
  end

  def test_scenario_bundle_refuses_an_empty_directory
    Dir.mktmpdir do |directory|
      output, error, status = Open3.capture3(RbConfig.ruby, File.join(ROOT, "scripts/simulator_scenarios.rb"), directory)

      refute status.success?
      assert_empty output
      assert_includes error, "No simulator scenarios"
    end
  end

  private
    def deployment
      source = File.read(File.join(ROOT, "server/config/deploy.yml"))
      YAML.safe_load(ERB.new(source).result)
    end
end
