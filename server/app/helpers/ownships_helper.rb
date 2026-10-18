module OwnshipsHelper
  GLASS = 200
  RINGS = [ 46, 92 ].freeze

  def ownship_limits
    { span: Ownship::SPAN, rows: Ownship::ROWS, rowsToNose: Ownship::ROWS_TO_NOSE }
  end

  def ownship_words
    scope = "briefing.ownships.show"
    {
      cell: t("cell", scope:), size: t("size_value", scope:), ink: t("ink_value", scope:),
      rows_count: t("rows_count", scope:), fits: t("fits", scope:), fidelity: t("fidelity", scope:),
      command_hint: t("command_hint", scope:), copied: t("copied", scope:),
      image_unreadable: t("image_unreadable", scope:), image_unkept: t("image_unkept", scope:),
      problems: t("briefing.ownships.problems")
    }
  end

  def ownship_source(source)
    safe_join(source.split(%r{(https?://\S+?)(?=,\s|\s|\z)}).map { it.match?(%r{\Ahttps?://}) ? link_to(it, it) : it })
  end
end
