namespace :legacy_dialect do
  desc "Write schemas/legacy_dialect.v1.json into app/javascript/skyblip/legacy_dialect_table.js, the copy the pages load"
  task :generate do
    table = JSON.parse(Rails.root.join("../schemas/legacy_dialect.v1.json").read)
    Rails.root.join("app/javascript/skyblip/legacy_dialect_table.js").write("export default #{JSON.pretty_generate(table)};\n")
  end
end
