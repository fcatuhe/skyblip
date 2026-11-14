import { ndjson } from '#skyblip/diagnostics';

// INFO: fc 15nov26 a full ring is about 7 MB of NDJSON, and gzip takes it under 1 MB
export async function gzipped(text) {
  const stream = new Blob([text]).stream().pipeThrough(new CompressionStream('gzip'));
  return new Blob([await new Response(stream).arrayBuffer()], { type: 'application/gzip' });
}

export function profileOf(lines) {
  return lines.find(line => line.type === 'boot')?.profile ?? 'full';
}

// INFO: fc 15nov26 the server reads the session, the records and the firmware off the file, so only the profile travels beside it
export async function sendCapture({ url, fetched, browser, token = null, fetch = globalThis.fetch }) {
  const form = new FormData();
  form.append('capture[profile]', profileOf(fetched.lines));
  form.append('capture[body]', await gzipped(ndjson(fetched.lines)), `diagnostics-${fetched.session.session}.ndjson.gz`);
  const response = await fetch(url, {
    method: 'POST',
    credentials: 'same-origin',
    headers: { 'X-Briefing-Browser': browser, ...(token ? { 'X-CSRF-Token': token } : {}) },
    body: form,
  });
  if (!response.ok) throw new Error(`the server answered ${response.status}`);
  return response.json();
}
