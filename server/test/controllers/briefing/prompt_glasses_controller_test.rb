require "test_helper"

class Briefing::PromptGlassesControllerTest < ActionDispatch::IntegrationTest
  test "the prompt glass loads the local build's embed script" do
    Dir.mktmpdir do |directory|
      Release::SIMULATOR_FILES.each { |file| FileUtils.touch(File.join(directory, file)) }

      with_config(simulator_build: directory) { get "/briefing/prompt_glass" }
    end

    assert_redirected_to "/releases/local/embed.js"
  end

  test "the prompt glass answers unavailable when no simulator can draw it" do
    with_config(simulator_build: nil, local_simulator_only: true) { get "/briefing/prompt_glass" }

    assert_response :service_unavailable
  end
end
