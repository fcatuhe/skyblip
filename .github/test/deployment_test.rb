require "minitest/autorun"
require "erb"
require "yaml"
require "json"
require "open3"
require "tmpdir"
require "fileutils"
require_relative "../../lib/firmware_tree"
require_relative "../../lib/radio_protocols"
require_relative "../../server/app/models/release"

class DeploymentTest < Minitest::Test
  ROOT = File.expand_path("../..", __dir__)

  def test_kamal_preserves_the_clean_clone_and_server_directory
    builder = deployment.fetch("builder")

    assert_equal "arm64", builder.fetch("arch")
    refute builder.key?("context")
    refute builder.key?("dockerfile")
  end

  def test_application_runs_as_debian_and_the_remote_builder_on_the_crooz_ssh_host
    config = deployment
    host = ENV.fetch("SKYBLIP_SERVER_IP", "crooz")

    assert_equal [ host ], config.fetch("servers").fetch("web")
    assert_equal "debian", config.fetch("ssh").fetch("user")
    assert_equal "localhost:5555", config.fetch("registry").fetch("server")
    if RUBY_PLATFORM.match?(/aarch64|arm64/)
      refute config.fetch("builder").key?("remote")
    else
      assert_equal "ssh://crooz", config.fetch("builder").fetch("remote")
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

  # What the development pre-release stages is lib/dev_release.rb's, tested in dev_release_test.rb.
  def test_development_publishes_every_product_through_the_commits_own_dev_release
    job = YAML.load_file(File.join(ROOT, ".github/workflows/firmware.yml")).fetch("jobs").fetch("dev-release")
    steps = job.fetch("steps")
    checkout = steps.find { |step| step["uses"].to_s.start_with?("actions/checkout") }

    assert steps.any? { |step| step.dig("with", "pattern") == "skyblip-*" && step.dig("with", "merge-multiple") }
    assert_equal %w[LICENSE NOTICE.md LICENSES firmware/protocols/*/version.h firmware/products/*/PROTOCOLS lib/dev_release.rb lib/radio_protocols.rb lib/release_files.rb lib/release_notice.rb scripts/dev_release.rb],
                 checkout.dig("with", "sparse-checkout").split
    assert_equal "ruby source/scripts/dev_release.rb image simulator source", steps.last.fetch("run")
  end

  # release.yml releases every product that declares a board, so each must be served and named in its release.
  def test_every_product_is_served_and_lists_its_radio_protocols
    FirmwareTree.new(File.join(ROOT, "firmware")).products.each do |product|
      slug = product.tr("_", "-")
      assert File.file?(File.join(ROOT, "firmware/products", product, "PROTOCOLS")), "#{product} has no PROTOCOLS"
      assert_equal product.delete_prefix("skyblip_"), Release.product_of("#{slug}-0.1.0.signed.bin"), "Release::FIRMWARE does not match #{slug}"
    end
  end

  # Before one release carried every product, Bravo's were tagged bravo-v*.
  def test_a_drafts_notes_start_at_the_last_published_release
    Dir.mktmpdir do |tree|
      fake_gh(tree, %w[bravo-v0.2.0 v0.1.0])
      draft = ->(tag) { run_step(tree, release_step("draft", "create the draft"), "PATH" => "#{tree}:#{ENV.fetch("PATH")}", "GITHUB_REF_NAME" => tag, "GITHUB_REPOSITORY" => "fcatuhe/skyblip") }

      assert_includes draft.("v0.2.0").first, "--generate-notes --notes-start-tag v0.1.0"

      fake_gh(tree, %w[bravo-v0.2.0])
      output, status = draft.("v0.1.0")
      assert status.success?, output
      assert_includes output, "gh release create v0.1.0"
      refute_includes output, "--notes-start-tag"
    end
  end

  def test_the_radio_protocols_notes_refuse_a_header_without_one_specification
    Dir.mktmpdir do |firmware|
      FileUtils.mkdir_p(File.join(firmware, "products/skyblip_bravo"))
      File.write(File.join(firmware, "products/skyblip_bravo/PROTOCOLS"), "adsb\n")
      FileUtils.mkdir_p(File.join(firmware, "protocols/adsb"))
      File.write(File.join(firmware, "protocols/adsb/version.h"), "inline constexpr char kVersion[] = \"0.1.0\";\n")
      error = assert_raises(ArgumentError) { RadioProtocols.new(firmware).notes(version: "0.2.0", products: %w[skyblip_bravo]) }
      assert_equal "No single kSpecification in protocols/adsb/version.h", error.message
    end
  end

  def test_the_jobs_holding_the_production_key_run_no_emscripten_and_write_nothing
    jobs = workflow("release").fetch("jobs")
    image = jobs.fetch("image")

    assert_includes image.fetch("needs"), "simulator"
    refute image.fetch("steps").any? { |step| step["uses"].to_s.include?("setup-emsdk") || step["run"].to_s.include?("make -C firmware browser") }
    refute image.key?("permissions")
    assert_equal({ "contents" => "read" }, workflow("release").fetch("permissions"))
    assert_equal [ "draft" ], jobs.select { |_name, job| job.dig("permissions", "contents") == "write" }.keys
  end

  def test_every_emsdk_setup_installs_the_version_the_pins_hold
    %w[firmware release server].each do |name|
      steps = workflow(name).fetch("jobs").values.flat_map { |job| job.fetch("steps", []) }
      setups = steps.select { |step| step["uses"].to_s.include?("setup-emsdk") }

      refute_empty setups, name
      setups.each { |step| assert_equal "${{ steps.emsdk.outputs.version }}", step.dig("with", "version"), name }
    end
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
    def workflow(name) = YAML.load_file(File.join(ROOT, ".github/workflows/#{name}.yml"))

    def release_step(job, name) = workflow("release").fetch("jobs").fetch(job).fetch("steps").find { |step| step["name"] == name }.fetch("run")

    def run_step(tree, script, env)
      output, status = Open3.capture2e(env, "bash", "-e", "-o", "pipefail", "-c", script, chdir: tree)
      [ output, status ]
    end

    def fake_gh(tree, published)
      File.write(File.join(tree, "gh"), <<~SH)
        #!/usr/bin/env bash
        if [ "$1 $2" = "release list" ]; then printf '%s\n' #{published.join(" ")}; else echo "gh $*"; fi
      SH
      File.chmod(0o755, File.join(tree, "gh"))
    end

    def deployment
      source = File.read(File.join(ROOT, "server/config/deploy.yml"))
      YAML.safe_load(ERB.new(source).result)
    end
end
