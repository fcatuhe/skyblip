let lock = null

// INFO: fc 11nov26 a phone that sleeps mid-upload drops the link, so the screen stays on while any image travels
export function stayAwake(uploading) {
  if (uploading && !lock && navigator.wakeLock) {
    lock = navigator.wakeLock.request("screen").then(held => {
      held.addEventListener("release", () => (lock = null), { once: true })
      return held
    }, error => {
      console.warn("The screen may sleep:", error.message)
      lock = null
    })
  } else if (!uploading && lock) {
    lock.then(held => held?.release())
    lock = null
  }
}
