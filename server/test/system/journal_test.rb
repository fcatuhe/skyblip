require "application_system_test_case"

class JournalTest < ApplicationSystemTestCase
  # Firefox on iOS, Dark Reader and wallet extensions run their scripts in the page, and their errors filled the list of what breaks.
  test "an error from a script injected into the page or from another origin is left out, and the site's own is told" do
    visit "/"

    page.execute_script(<<~JS)
      dispatchEvent(new ErrorEvent("error", { message: "Can't find variable: __firefox__", filename: location.href, lineno: 1, colno: 12,
                                              error: new ReferenceError("Can't find variable: __firefox__") }))
      dispatchEvent(new ErrorEvent("error", { message: "Script error.", lineno: 0, colno: 0 }))
      dispatchEvent(new ErrorEvent("error", { message: "the gauge broke", filename: `${location.origin}/assets/application.js`, lineno: 3, colno: 7,
                                              error: new TypeError("the gauge broke") }))
    JS

    assert_equal "the gauge broke", journaled("browser.failed").payload["message"]
    assert_equal true, page.evaluate_async_script(JOURNAL_FLUSHED)
    assert_equal [ "the gauge broke" ], Event.where(name: "browser.failed").map { it.payload["message"] }
  end
end
