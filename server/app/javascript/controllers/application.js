import { Application } from "@hotwired/stimulus"

const application = Application.start()

// Configure Stimulus development experience
application.debug = false
window.Stimulus   = application

// INFO: fc 14nov26 Stimulus only logs an error it caught in a controller, so the journal hears it through this event
application.handleError = (error, message, detail) => {
  console.error(message, error, detail)
  dispatchEvent(new CustomEvent("stimulus:failed", { detail: { error, identifier: detail?.identifier } }))
}

export { application }
