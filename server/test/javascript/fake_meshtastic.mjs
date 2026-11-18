import { FrameReader, fieldsOf, frame } from '#skyblip/meshtastic';

function varint(value) {
  const bytes = [];
  let rest = value >>> 0;
  while (rest > 0x7f) {
    bytes.push((rest & 0x7f) | 0x80);
    rest >>>= 7;
  }
  bytes.push(rest);
  return bytes;
}

const lengthDelimited = (field, bytes) => [...varint((field << 3) | 2), ...varint(bytes.length), ...bytes];
const unsigned = (field, value) => [...varint(field << 3), ...varint(value)];

export const myInfo = nodeNum => frame(lengthDelimited(3, unsigned(1, nodeNum)));
export const metadata = (hwModel, version = '2.7.15.567b8ea') => frame(lengthDelimited(13, [...lengthDelimited(1, [...new TextEncoder().encode(version)]), ...unsigned(9, hwModel)]));
export const configComplete = nonce => frame(unsigned(7, nonce));
export const logLine = text => new TextEncoder().encode(`${text}\r\n`);

// INFO: fc 17nov26 a node on a Web Serial port: it logs text until it hears a frame, answers want_config, and restarts on enter_dfu_mode_request
export class FakeNode {
  constructor({ nodeNum = 0x9a3f2a, hwModel = 7, answers = true } = {}) {
    Object.assign(this, { nodeNum, hwModel, answers, heard: [], restarted: false });
    let push;
    this.readable = new ReadableStream({ start: controller => (push = controller) });
    this.push = bytes => push.enqueue(bytes);
    this.closeReadable = () => push.close();
    const frames = new FrameReader(payload => this.#take(payload));
    this.writable = new WritableStream({ write: bytes => frames.feed(bytes) });
    this.push(logLine('INFO  | ??:??:?? 2 [Router] Booting'));
  }

  #take(payload) {
    const fields = fieldsOf(payload);
    this.heard.push(fields);
    const [number, value] = fields[0];
    if (number === 3 && this.answers) {
      this.push(myInfo(this.nodeNum).subarray(0, 3));
      this.push(myInfo(this.nodeNum).subarray(3));
      this.push(logLine('DEBUG | ??:??:?? 3 [Router] Sending config'));
      this.push(metadata(this.hwModel));
      this.push(configComplete(value));
    }
    if (number === 1) this.restarted = true;
  }
}
