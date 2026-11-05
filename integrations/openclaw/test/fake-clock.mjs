// A stand-in clock: the parts of the firmware's HTTP API the bridge uses, with
// the same rules for who may call what (messages and /status open; faces and
// variables need the API key; /api/set's theme and brightness need the Home
// Assistant integration on). Records every request.

import { createServer } from "node:http";

export class FakeClock {
  constructor({ apiKey = "k".repeat(32), haCompat = false } = {}) {
    this.apiKey = apiKey;
    this.haCompat = haCompat;
    this.requests = [];
    this.messages = new Map();
    this.vars = {};
    this.faces = {};
    this.face = 0;
    this.custom = "";
    this.brightness = 80;
    this.server = createServer((req, res) => this.handle(req, res));
  }

  listen() { return new Promise((ok) => this.server.listen(0, "127.0.0.1", () => { this.url = `http://127.0.0.1:${this.server.address().port}`; ok(this); })); }
  close() { this.server.closeAllConnections?.(); return new Promise((ok) => this.server.close(() => ok())); }

  handle(req, res) {
    let body = "";
    req.on("data", (d) => (body += d));
    req.on("end", () => {
      const u = new URL(req.url, "http://x");
      const rec = { method: req.method, path: u.pathname, query: Object.fromEntries(u.searchParams), body, key: req.headers["x-api-key"] || "" };
      this.requests.push(rec);
      const send = (code, data) => { res.writeHead(code, { "Content-Type": typeof data === "string" ? "text/plain" : "application/json" }); res.end(typeof data === "string" ? data : JSON.stringify(data)); };
      const keyed = () => rec.key === this.apiKey;
      const q = rec.query;
      if (u.pathname === "/status") return send(200, { version: "2.2.0", build: "fake", uptime: 42, heap: 15000, ip: "127.0.0.1", rssi: -55, connected: true, host: "agentmonitor-fake", face: this.face, customFace: this.custom || undefined });
      if (u.pathname === "/api/message") {
        if (req.method === "POST") { const m = JSON.parse(body); this.messages.set(m.id || "agent", m); if (m.show) this.face = 15; return send(200, "OK"); }
        if (req.method === "DELETE") { if (q.id) this.messages.delete(q.id); else this.messages.clear(); return send(200, "OK"); }
        return send(200, { messages: [...this.messages.values()].map((m) => ({ ...m, ttl: m.ttl ?? 600, showing: false })) });
      }
      if (u.pathname.startsWith("/api/faces")) {
        if (!keyed()) return send(401, "Sign in or send the API key (X-Api-Key)");
        if (u.pathname === "/api/faces/vars") { Object.assign(this.vars, JSON.parse(body)); return send(200, "OK"); }
        if (u.pathname === "/api/faces/show") { if (!this.faces[q.name]) return send(404, "No such face"); this.face = 14; this.custom = q.name; return send(200, "OK"); }
        if (u.pathname === "/api/faces" && req.method === "POST") { this.faces[q.name] = JSON.parse(body); if (q.show) { this.face = 14; this.custom = q.name; } return send(200, "OK"); }
        if (u.pathname === "/api/faces") return send(200, { faces: Object.keys(this.faces), showing: this.custom, vars: this.vars, persist: [] });
      }
      if (u.pathname === "/api/set") {
        if (!this.haCompat) return send(401, "Sign in first");
        if (q.key === "theme") this.face = Number(q.value);
        if (q.key === "lcd_brightness") this.brightness = Number(q.value);
        return send(200, "OK");
      }
      send(404, "Not found");
    });
  }
}
