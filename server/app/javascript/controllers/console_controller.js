import { Controller } from "@hotwired/stimulus"
import { connect } from "#skyblip/ble"
import { hex, printable } from "#skyblip/link"

const COMMANDS = ["get", "defaults", "status", "timing", "flash", "radio", "update", "diagnostics"]

export default class extends Controller {
  connect() {
    this.session = new AbortController()
    this.device = null
    this.listen("connect", "click", () => this.link())
    this.listen("disconnect", "click", () => this.device?.disconnect())
    this.listen("send", "submit", event => this.sendLine(event))
    this.listen("sendjson", "submit", event => this.sendJson(event))
    this.listen("clearstream", "click", () => { this.field("stream").textContent = "-" })
    this.field("commands").replaceChildren(...COMMANDS.map(command => {
      const button = document.createElement("button")
      button.type = "button"
      button.textContent = command
      button.addEventListener("click", () => this.command({ command }), { signal: this.session.signal })
      return button
    }))
    if (!navigator.bluetooth) {
      this.field("connect").disabled = true
      this.state("Web Bluetooth is unavailable in this browser", "down")
    }
  }

  disconnect() {
    this.session.abort()
    this.device?.disconnect()
    this.device = null
  }

  async link() {
    const { signal } = this.session
    this.field("connect").disabled = true
    try {
      const device = await connect({
        onLine: line => { if (!signal.aborted) this.write("stream", line) },
        onReply: (frame, bytes, error) => {
          if (signal.aborted) return
          if (frame) this.write("replies", printable(frame), "ok")
          else this.write("replies", `${error.message}: ${hex(bytes)}`, "warn")
        },
        onClose: name => { if (!signal.aborted) this.closed(name) }
      })
      if (signal.aborted) {
        device.disconnect()
      } else {
        this.device = device
        this.state(`${device.name} over ${device.transport}`, "up")
        this.field("disconnect").disabled = false
        this.field("configpanel").hidden = !device.hasConfig
      }
    } catch (error) {
      if (!signal.aborted) {
        this.field("connect").disabled = false
        this.state(error.message, "down")
      }
    }
  }

  closed(name) {
    this.device = null
    this.state(`${name || "device"} disconnected`, "down")
    this.field("connect").disabled = false
    this.field("disconnect").disabled = true
    this.field("configpanel").hidden = true
  }

  async sendLine(event) {
    event.preventDefault()
    if (!this.device) return this.state("not connected", "down")
    try {
      await this.device.sendLine(this.field("line").value)
      this.write("stream", "> " + this.field("line").value, "out")
      this.field("line").value = ""
    } catch (error) {
      this.write("stream", error.message, "bad")
    }
  }

  async sendJson(event) {
    event.preventDefault()
    let body
    try {
      body = JSON.parse(this.field("json").value)
    } catch (error) {
      this.write("replies", "not JSON: " + error.message, "bad")
      return
    }
    await this.command(body)
    this.field("json").value = ""
  }

  async command(body) {
    if (!this.device) return this.state("not connected", "down")
    try {
      await this.device.sendConfig(body)
      this.write("replies", "> " + printable(body), "out")
    } catch (error) {
      this.write("replies", error.message, "bad")
    }
  }

  write(id, text, kind = "") {
    const tape = this.field(id)
    if (tape.textContent === "-") tape.textContent = ""
    if (this.field("stamps").checked) {
      const at = document.createElement("span")
      at.className = "at"
      at.textContent = new Date().toTimeString().slice(0, 8) + " "
      tape.append(at)
    }
    const line = document.createElement("span")
    line.className = kind
    line.textContent = text + "\n"
    tape.append(line)
    if (this.field("follow").checked) tape.scrollTop = tape.scrollHeight
  }

  state(text, kind) {
    this.field("state").textContent = text
    this.field("state").className = "state" + (kind ? " " + kind : "")
  }

  listen(id, event, callback) {
    this.field(id).addEventListener(event, callback, { signal: this.session.signal })
  }

  field(id) {
    return this.element.querySelector(`#${id}`)
  }
}
