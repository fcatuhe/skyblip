class GetInsightsTool < ApplicationTool
  tool_name "get_insights"
  title "Read how the briefing app and skyblip.eu are used"
  description "The numbers skyBlip keeps on its own use over the last days, against the period before: browsers that reach " \
              "the briefing app and those with no Bluetooth, devices connected and how long it takes, links dropped mid-task, " \
              "device choosers closed empty, the notices pilots read, tasks granted and refused, installs started, succeeded " \
              "and failed by cause, the firmware the fleet runs, activations, the browsers whose last step was a failure, " \
              "errors, slow requests, failing jobs and agents' calls. Read it to find what to improve, and again after a change."
  input_schema properties: {
    days: { type: "integer", enum: Event::Review::PERIODS, description: "The period, #{Event::Review::PERIODS.first} unless said" }
  }
  annotations(**READING)

  def self.call(server_context:, days: Event::Review::PERIODS.first)
    if admin(server_context)
      review = Event::Review.new(since: days.to_i.days.ago)
      json now: review.to_h, before: review.previous.headline
    else
      admins_only
    end
  end
end
