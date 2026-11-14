namespace :events do
  desc "Print the numbers to review over the last DAYS (default 7)"
  task review: :environment do
    pp Event::Review.new(since: Integer(ENV.fetch("DAYS", "7")).days.ago).to_h
  end

  desc "Print the LAST events (default 200) as JSON lines, narrowed to a NAME prefix, a DEVICE, a support CODE or a FINGERPRINT"
  task tail: :environment do
    puts Event.tail(name: ENV["NAME"], device: ENV["DEVICE"], browser: ENV["CODE"]&.delete("-")&.upcase, fingerprint: ENV["FINGERPRINT"],
      last: Integer(ENV.fetch("LAST", Event::LIMIT.to_s))).map(&:to_line)
  end
end
