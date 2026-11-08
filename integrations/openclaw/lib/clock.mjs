// The clock's own HTTP API (docs/specifications/integrations-api.md), as the
// bridge uses it. Messages and /status need nothing; faces and variables need
// the faces API key (Integrations -> Custom faces); brightness and switching to
// a built-in face need the "Home Assistant dashboards" integration on (it opens
// /api/set's theme and brightness keys to the network). The bridge never signs
// in with the clock's password.

import { InvokeError } from "./protocol.mjs";

export class Clock {
  constructor({ name, url, apiKey = "", timeoutMs = 8000 }) {
    if (!/^https?:\/\//.test(url || "")) throw new Error(`clock ${name}: url must start with http:// or https://`);
    this.name = name;
    this.base = url.replace(/\/+$/, "");
    this.apiKey = apiKey;
    this.timeoutMs = timeoutMs;
  }

  /** One request; JSON in and out. Errors become InvokeErrors the agent can act on. */
  async request(method, path, { body, key = false, need = "", accept } = {}) {
    if (key && !this.apiKey) {
      throw new InvokeError("NOT_CONFIGURED", `${this.name}: no faces API key in the bridge's config (clock settings: Integrations -> Custom faces)`);
    }
    const headers = {};
    if (key) headers["X-Api-Key"] = this.apiKey;
    if (accept) headers["Accept-Encoding"] = accept;
    let payload;
    if (body !== undefined) {
      headers["Content-Type"] = "application/json";
      payload = typeof body === "string" ? body : JSON.stringify(body);
    }
    let res;
    try {
      res = await fetch(this.base + path, { method, headers, body: payload, signal: AbortSignal.timeout(this.timeoutMs) });
    } catch (e) {
      throw new InvokeError("UNAVAILABLE", `${this.name} (${this.base}) did not answer: ${e.cause?.code || e.name || e.message}`);
    }
    const text = await res.text();
    if (res.status === 401) {
      throw new InvokeError("NOT_CONFIGURED", need
        ? `${this.name} refused: ${need}`
        : `${this.name} refused the faces API key (is Integrations -> Custom faces on, and the key current?)`);
    }
    if (res.status === 507 || res.status === 503) throw new InvokeError("UNAVAILABLE", `${this.name} is short of memory; try again (${text.trim()})`);
    if (res.status === 400) throw new InvokeError("INVALID_PARAMS", `${this.name}: ${text.trim() || "bad request"}`);
    if (!res.ok) throw new InvokeError("CLOCK_ERROR", `${this.name} answered ${res.status}: ${text.trim().slice(0, 200)}`);
    if (!text) return {};
    try { return JSON.parse(text); } catch { return { text: text.trim() }; }
  }

  status() { return this.request("GET", "/status"); }
  messages() { return this.request("GET", "/api/message"); }
  postMessage(msg) { return this.request("POST", "/api/message", { body: msg }); }
  deleteMessage(id) { return this.request("DELETE", "/api/message" + (id ? `?id=${encodeURIComponent(id)}` : "")); }
  setVars(vars) { return this.request("POST", "/api/faces/vars", { body: vars, key: true }); }
  faces() { return this.request("GET", "/api/faces", { key: true }); }
  showFace(name) { return this.request("POST", `/api/faces/show?name=${encodeURIComponent(name)}`, { key: true }); }
  // The json-render catalog's prompt (spec 047): stored gzipped on the clock, which answers
  // 406 to a client that does not take gzip, and 404 when its firmware has no json-render.
  catalogPrompt() { return this.request("GET", "/api/catalog/prompt", { accept: "gzip" }); }
  saveFace(name, face, show) {
    return this.request("POST", `/api/faces?name=${encodeURIComponent(name)}${show ? "&show=1" : ""}`, { body: face, key: true });
  }
  setCompat(key, value) {
    return this.request("GET", `/api/set?key=${key}&value=${encodeURIComponent(value)}`, {
      need: "turn on Integrations -> Home Assistant dashboards on the clock to let the bridge set brightness and built-in faces",
    });
  }
}
