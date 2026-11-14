// INFO: fc 13nov26 the unit answers in order, so the first waiter that a reply matches is the one it answers
export class Replies {
  #send;
  #withinMs;
  #waiting = [];
  #generation = 0;
  #opened = 0;

  constructor({ send, withinMs }) {
    this.#send = send;
    this.#withinMs = withinMs;
  }

  open() {
    this.#opened = this.#generation;
  }

  take(reply) {
    const index = this.#waiting.findIndex(({ matches }) => matches(reply));
    if (index < 0) return false;
    const [waiter] = this.#waiting.splice(index, 1);
    clearTimeout(waiter.timer);
    waiter.resolve(reply);
    return true;
  }

  close() {
    this.#generation++;
    for (const waiter of this.#waiting.splice(0)) {
      clearTimeout(waiter.timer);
      waiter.resolve(null);
    }
  }

  // INFO: fc 13nov26 a request from before a reconnect is never sent, so no stale waiter takes a new reply
  async ask(command, matches) {
    if (this.#generation !== this.#opened) return null;
    let waiter;
    const answered = new Promise(resolve => {
      waiter = { matches, resolve };
      waiter.timer = setTimeout(() => this.#drop(waiter), this.#withinMs);
      this.#waiting.push(waiter);
    });
    try {
      await this.#send(command);
    } catch (error) {
      this.#drop(waiter);
      throw error;
    }
    return answered;
  }

  #drop(waiter) {
    const index = this.#waiting.indexOf(waiter);
    if (index >= 0) this.#waiting.splice(index, 1);
    clearTimeout(waiter.timer);
    waiter.resolve(null);
  }
}
