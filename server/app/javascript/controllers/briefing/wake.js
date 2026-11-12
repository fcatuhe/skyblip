let lock = null

// INFO: fc 11nov26 a phone that sleeps mid-upload drops the link, so the screen stays on while any image travels
export function stayAwake(uploading) {
  if (uploading && !lock && navigator.wakeLock) {
    lock = navigator.wakeLock.request("screen").catch(error => console.warn("The screen may sleep:", error.message))
  } else if (!uploading && lock) {
    lock.then(held => held?.release())
    lock = null
  }
}
