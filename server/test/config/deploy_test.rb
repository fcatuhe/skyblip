require "test_helper"

class DeployTest < ActiveSupport::TestCase
  test "kamal builds the clean clone of server for arm64" do
    builder = deployment.fetch("builder")

    assert_equal "arm64", builder.fetch("arch")
    assert_not builder.key?("context")
    assert_not builder.key?("dockerfile")
  end

  test "the application runs as debian, and the remote builder on the crooz ssh host" do
    config = deployment

    assert_equal [ ENV.fetch("SKYBLIP_SERVER_IP", "crooz") ], config.fetch("servers").fetch("web")
    assert_equal "debian", config.fetch("ssh").fetch("user")
    assert_equal "localhost:5555", config.fetch("registry").fetch("server")
    if RUBY_PLATFORM.match?(/aarch64|arm64/)
      assert_not config.fetch("builder").key?("remote")
    else
      assert_equal "ssh://crooz", config.fetch("builder").fetch("remote")
    end
  end

  test "sqlite and solid queue live in the persistent volume" do
    assert_equal [ "skyblip_storage:/rails/storage" ], deployment.fetch("volumes")
    assert deployment.fetch("env").fetch("clear").fetch("SOLID_QUEUE_IN_PUMA")
    assert_equal [ "RAILS_MASTER_KEY" ], deployment.fetch("env").fetch("secret")
  end

  private
    def deployment = YAML.safe_load(ERB.new(Rails.root.join("config/deploy.yml").read).result)
end
