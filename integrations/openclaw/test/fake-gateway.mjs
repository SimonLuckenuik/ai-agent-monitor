// A stand-in OpenClaw Gateway for tests: a minimal WebSocket server (RFC 6455,
// text frames) that sends connect.challenge, checks the node's connect the way
// the gateway does (protocol range, client id and mode, device id = sha256 of the
// public key, the v3 signature rebuilt byte for byte, the credential), answers
// hello-ok with a device token, and lets a test send node.invoke.request and
// await the node.invoke.result.

import { createHash, createPublicKey, randomBytes, randomUUID, verify } from "node:crypto";
import { createServer } from "node:http";
import { EventEmitter } from "node:events";

// The gateway's own payload builder, copied from openclaw/openclaw
// packages/gateway-client/src/device-auth.ts (MIT), so the bridge's builder is
// checked against theirs rather than against itself.
function normalizeDeviceMetadataForAuth(value) {
  if (typeof value !== "string") return "";
  return value.trim().replace(/[A-Z]/g, (char) => String.fromCharCode(char.charCodeAt(0) + 32));
}
export function buildDeviceAuthPayloadV3(params) {
  const scopes = params.scopes.join(",");
  const token = params.token ?? "";
  const platform = normalizeDeviceMetadataForAuth(params.platform);
  const deviceFamily = normalizeDeviceMetadataForAuth(params.deviceFamily);
  return ["v3", params.deviceId, params.clientId, params.clientMode, params.role, scopes,
    String(params.signedAtMs), token, params.nonce, platform, deviceFamily].join("|");
}

const GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

function encodeFrame(text, opcode = 1) {
  const data = Buffer.from(text, "utf8");
  const len = data.length;
  const head = len < 126 ? Buffer.from([0x80 | opcode, len])
    : len < 65536 ? Buffer.from([0x80 | opcode, 126, len >> 8, len & 255])
    : (() => { const b = Buffer.alloc(10); b[0] = 0x80 | opcode; b[1] = 127; b.writeBigUInt64BE(BigInt(len), 2); return b; })();
  return Buffer.concat([head, data]);
}

/** Reads client frames (always masked) from a growing buffer. */
function* decodeFrames(state) {
  for (;;) {
    const b = state.buf;
    if (b.length < 2) return;
    const opcode = b[0] & 15;
    let len = b[1] & 127, off = 2;
    if (len === 126) { if (b.length < 4) return; len = b.readUInt16BE(2); off = 4; }
    else if (len === 127) { if (b.length < 10) return; len = Number(b.readBigUInt64BE(2)); off = 10; }
    const masked = b[1] & 128;
    const need = off + (masked ? 4 : 0) + len;
    if (b.length < need) return;
    let payload = b.subarray(off + (masked ? 4 : 0), need);
    if (masked) { const m = b.subarray(off, off + 4); payload = Buffer.from(payload.map((x, i) => x ^ m[i & 3])); }
    state.buf = b.subarray(need);
    yield { opcode, payload };
  }
}

export class FakeGateway extends EventEmitter {
  /**
   * options: token (shared secret accepted), bootstrapToken (setup-code token
   * accepted once), deviceToken (issued in hello-ok), requirePairing (refuse
   * unknown devices until approve(deviceId)).
   */
  constructor(options = {}) {
    super();
    this.o = { token: "gw-shared-token", bootstrapToken: "", deviceToken: "dev-token-" + randomUUID(), ...options };
    this.approved = new Set();
    this.sockets = new Set();
    this.nodes = new Map();          // deviceId -> {socket, params}
    this.pending = new Map();        // invoke id -> resolve
    this.connects = [];              // every connect request seen
    this.server = createServer((req, res) => { res.writeHead(426); res.end(); });
    this.server.on("upgrade", (req, socket) => this.upgrade(req, socket));
  }

  listen() {
    return new Promise((ok) => this.server.listen(0, "127.0.0.1", () => { this.url = `ws://127.0.0.1:${this.server.address().port}`; ok(this); }));
  }

  close() {
    for (const s of this.sockets) s.destroy();
    return new Promise((ok) => this.server.close(() => ok()));
  }

  approve(deviceId) { this.approved.add(deviceId); }

  upgrade(req, socket) {
    const key = req.headers["sec-websocket-key"];
    const accept = createHash("sha1").update(key + GUID).digest("base64");
    socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
    this.sockets.add(socket);
    const conn = { socket, nonce: randomBytes(16).toString("hex"), ts: Date.now(), state: { buf: Buffer.alloc(0) }, deviceId: null };
    socket.on("data", (d) => {
      conn.state.buf = Buffer.concat([conn.state.buf, d]);
      for (const f of decodeFrames(conn.state)) {
        if (f.opcode === 8) { socket.end(encodeFrame("", 8)); continue; }
        if (f.opcode === 9) { socket.write(encodeFrame(f.payload.toString(), 10)); continue; }
        if (f.opcode === 1) this.onFrame(conn, JSON.parse(f.payload.toString("utf8")));
      }
    });
    socket.on("close", () => { this.sockets.delete(socket); if (conn.deviceId && this.nodes.get(conn.deviceId)?.socket === socket) this.nodes.delete(conn.deviceId); this.emit("disconnect", conn.deviceId); });
    socket.on("error", () => {});
    this.send(socket, { type: "event", event: "connect.challenge", payload: { nonce: conn.nonce, ts: conn.ts } });
  }

  send(socket, frame) { socket.write(encodeFrame(JSON.stringify(frame))); }

  /** Closes a node's socket, as a gateway restart would. */
  drop(deviceId) { const n = this.nodes.get(deviceId); if (n) n.socket.destroy(); }

  refuse(conn, id, code, message, details) {
    this.send(conn.socket, { type: "res", id, ok: false, error: { code, message, ...(details ? { details } : {}) } });
    setTimeout(() => conn.socket.end(encodeFrame("", 8)), 10);
  }

  checkConnect(conn, p) {
    if (!(p.minProtocol <= 4 && p.maxProtocol >= 3)) return "protocol";
    if (p.client?.id !== "node-host" || p.client?.mode !== "node" || p.role !== "node") return "client";
    const d = p.device || {};
    if (d.nonce !== conn.nonce || d.signedAt !== conn.ts) return "nonce";
    const raw = Buffer.from(d.publicKey || "", "base64url");
    if (raw.length !== 32) return "publicKey";
    if (createHash("sha256").update(raw).digest("hex") !== d.id) return "deviceId";
    const auth = p.auth || {};
    const kind = Object.keys(auth)[0];
    const token = kind === "password" ? "" : auth[kind] || "";
    const payload = buildDeviceAuthPayloadV3({
      deviceId: d.id, clientId: p.client.id, clientMode: p.client.mode, role: p.role, scopes: p.scopes || [],
      signedAtMs: d.signedAt, token, nonce: d.nonce, platform: p.client.platform, deviceFamily: p.client.deviceFamily,
    });
    const key = createPublicKey({ key: { kty: "OKP", crv: "Ed25519", x: d.publicKey }, format: "jwk" });
    if (!verify(null, Buffer.from(payload), key, Buffer.from(d.signature || "", "base64url"))) return "signature";
    return "";
  }

  onFrame(conn, frame) {
    if (frame.type !== "req") return;
    if (frame.method === "connect") {
      const p = frame.params || {};
      this.connects.push(p);
      const bad = this.checkConnect(conn, p);
      if (bad) return this.refuse(conn, frame.id, "INVALID_REQUEST", `device auth failed: ${bad}`, { code: "DEVICE_AUTH_INVALID" });
      const auth = p.auth || {};
      const id = p.device.id;
      let ok = false;
      if (auth.deviceToken) ok = auth.deviceToken === this.o.deviceToken && this.approved.has(id);
      else if (auth.bootstrapToken) { ok = !!this.o.bootstrapToken && auth.bootstrapToken === this.o.bootstrapToken; if (ok) { this.o.bootstrapToken = ""; this.approved.add(id); } }
      else if (auth.token) ok = auth.token === this.o.token;
      if (!ok) return this.refuse(conn, frame.id, "UNAUTHORIZED", "auth failed", { code: "AUTH_TOKEN_MISMATCH" });
      if (this.o.requirePairing && !this.approved.has(id)) {
        this.emit("pairing", id);
        return this.refuse(conn, frame.id, "NOT_PAIRED", "pairing required", { code: "PAIRING_REQUIRED", requestId: "req-1" });
      }
      this.approved.add(id);
      conn.deviceId = id;
      this.nodes.set(id, { socket: conn.socket, params: p });
      this.send(conn.socket, {
        type: "res", id: frame.id, ok: true,
        payload: {
          type: "hello-ok", protocol: 4, server: { version: "fake", connId: randomUUID() },
          features: { methods: [], events: [] }, snapshot: { big: "x".repeat(200000) },
          policy: { maxPayload: 26214400, maxBufferedBytes: 52428800, tickIntervalMs: 15000 },
          auth: {
            role: "node", scopes: [], deviceToken: this.o.deviceToken,
            deviceTokens: [{ role: "operator", deviceToken: "operator-handoff-token", scopes: ["operator.read", "operator.write"] }],
          },
        },
      });
      this.emit("node", id, p);
      return;
    }
    if (frame.method === "node.invoke.result") {
      const r = frame.params || {};
      this.send(conn.socket, { type: "res", id: frame.id, ok: true, payload: { ok: true } });
      const done = this.pending.get(r.id);
      if (done) { this.pending.delete(r.id); done(r); }
      return;
    }
    this.send(conn.socket, { type: "res", id: frame.id, ok: false, error: { code: "UNKNOWN_METHOD", message: frame.method } });
  }

  /** Sends node.invoke.request to a connected node; resolves with its result params (payload parsed). */
  invoke(deviceId, command, params, { raw } = {}) {
    const n = this.nodes.get(deviceId);
    if (!n) throw new Error(`node ${deviceId} not connected`);
    const id = "inv_" + randomUUID();
    return new Promise((ok) => {
      this.pending.set(id, (r) => ok({ ...r, payload: r.payloadJSON ? JSON.parse(r.payloadJSON) : r.payload }));
      this.send(n.socket, { type: "event", event: "node.invoke.request",
        payload: { id, nodeId: deviceId, command, paramsJSON: raw ?? JSON.stringify(params ?? {}), timeoutMs: 10000 } });
    });
  }

  waitFor(event, pred = () => true) {
    return new Promise((ok) => { const f = (...a) => { if (pred(...a)) { this.off(event, f); ok(a); } }; this.on(event, f); });
  }
}
