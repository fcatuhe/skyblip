namespace :ownship do
  desc "Compile the light aircraft drawing into firmware/ui/widgets/skyship.cpp, the symbol every device shows today"
  task compile: :environment do
    widget = Ownship::Widget.new
    changed = widget.compile(Ownship::AircraftType.default.ownship)
    puts changed ? "#{widget.path}: rewritten, now run make -C firmware test" : "#{widget.path}: unchanged"
  end
end
