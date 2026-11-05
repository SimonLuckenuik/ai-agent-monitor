// OpenClaw node protocol, the node side: an Ed25519 device identity, the signed
// `connect` answer to `connect.challenge`, and the `node.invoke.request` ->
// `node.invoke.result` loop. Written from the gateway's protocol docs
// (docs/gateway/protocol/handshake.md, packages/gateway-protocol/src/schema/nodes.ts)
// and the reference ESP32 node (openclaw/esp-openclaw-node 38c5ab2). Node 22+ only:
// the WebSocket client and Ed25519 are built in, so there is nothing to install.

import { createHash, createPrivateKey, createPublicKey, randomBytes, sign } from "node:crypto";
import { EventEmitter } from "node:events";

export const MIN_PROTOCOL = 3;   // nodes are accepted from protocol 3 (MIN_NODE_PROTOCOL_VERSION)
export const MAX_PROTOCOL = 4;   // the current protocol (PROTOCOL_VERSION)
export const CLIENT_ID = "node-host";   // one of the gateway's fixed client ids (GATEWAY_CLIENT_IDS)
export const CLIENT_MODE = "node";
export const ROLE = "node";

const b64url = (buf) => Buffer.from(buf).toString("base64url");   // no padding, like the gateway
const PKCS8_ED25519 = Buffer.from("302e020100300506032b657004220420", "hex");   // DER prefix of a raw seed

/** An Ed25519 identity from a 32-byte seed (new one when none is given). */
export function makeIdentity(seed = randomBytes(32)) {
  seed = Buffer.from(seed);
  if (seed.length !== 32) throw new Error("an Ed25519 seed is 32 bytes");
  const privateKey = createPrivateKey({ key: Buffer.concat([PKCS8_ED25519, seed]), format: "der", type: "pkcs8" });
  const rawPublic = Buffer.from(createPublicKey(privateKey).export({ format: "jwk" }).x, "base64url");
  return {
    seed,
    privateKey,
    publicKey: b64url(rawPublic),
    deviceId: createHash("sha256").update(rawPublic).digest("hex"),   // as the ESP32 node and the gateway derive it
  };
}

/** Lower-case and trim, as the gateway does before it compares signed metadata. */
export function normalizeMeta(value) {
  return typeof value === "string" ? value.trim().replace(/[A-Z]/g, (c) => c.toLowerCase()) : "";
}

/** The v3 device-auth payload: what the node signs and the gateway rebuilds byte for byte. */
export function authPayloadV3({ deviceId, clientId, clientMode, role, scopes, signedAtMs, token, nonce, platform, deviceFamily }) {
  return ["v3", deviceId, clientId, clientMode, role, scopes.join(","), String(signedAtMs),
    token ?? "", nonce, normalizeMeta(platform), normalizeMeta(deviceFamily)].join("|");
}

export function signPayload(identity, payload) {
  return b64url(sign(null, Buffer.from(payload, "utf8"), identity.privateKey));
}

/**
 * The `connect` request. `auth` is {kind, value}: kind `bootstrapToken` (from a
 * setup code), `token` (the gateway's shared token), `password`, or `deviceToken`
 * (saved from an earlier hello-ok). The password is not part of the signature.
 */
export function connectRequest({ id, identity, nonce, signedAtMs, auth, client, commands, caps = [], scopes = [], locale = "en-US", userAgent }) {
  const signatureToken = auth && auth.kind !== "password" ? auth.value : "";
  const payload = authPayloadV3({
    deviceId: identity.deviceId, clientId: CLIENT_ID, clientMode: CLIENT_MODE, role: ROLE, scopes,
    signedAtMs, token: signatureToken, nonce, platform: client.platform, deviceFamily: client.deviceFamily,
  });
  const params = {
    minProtocol: MIN_PROTOCOL,
    maxProtocol: MAX_PROTOCOL,
    client: { id: CLIENT_ID, mode: CLIENT_MODE, ...client },
    role: ROLE,
    scopes,
    caps,
    commands,
    locale,
    userAgent,
    device: { id: identity.deviceId, publicKey: identity.publicKey, signature: signPayload(identity, payload), signedAt: signedAtMs, nonce },
  };
  if (auth && auth.value) params.auth = { [auth.kind]: auth.value };
  return { type: "req", id, method: "connect", params };
}

/** Decodes a setup code (`openclaw qr --setup-code-only`): base64url JSON {url, bootstrapToken|token|password}. */
export function decodeSetupCode(code) {
  let data;
  try { data = JSON.parse(Buffer.from(String(code).trim(), "base64url").toString("utf8")); }
  catch { throw new Error("not a setup code (base64url JSON expected)"); }
  if (!data || typeof data.url !== "string") throw new Error("setup code has no gateway url");
  const kinds = ["bootstrapToken", "token", "password"].filter((k) => typeof data[k] === "string" && data[k]);
  if (kinds.length !== 1) throw new Error("setup code needs exactly one of bootstrapToken, token, password");
  return { url: data.url, auth: { kind: kinds[0], value: data[kinds[0]] } };
}

export class InvokeError extends Error {
  constructor(code, message) { super(message); this.code = code; }
}

/**
 * One node's connection to the gateway, kept up with reconnects.
 *
 * options: url, identity, auth (as for connectRequest; replaced by the device
 * token once the gateway issues one), client {displayName, version, platform,
 * deviceFamily, modelIdentifier}, commands (names), handle(command, params, meta)
 * -> result object (throw InvokeError for a coded failure), onDeviceToken(token),
 * log(level, msg).
 *
 * Events: "ready" (hello-ok payload), "closed" ({code, reason}), "rejected"
 * (connect error: auth problems stop the reconnects).
 */
export class NodeConnection extends EventEmitter {
  constructor(options) {
    super();
    this.o = { invokeTimeoutMs: 20000, reconnectMinMs: 1000, reconnectMaxMs: 30000, challengeTimeoutMs: 10000, ...options };
    this.auth = this.o.auth;
    this.log = this.o.log || (() => {});
    this.ws = null;
    this.seq = 0;
    this.stopped = true;
    this.ready = false;
    this.backoff = this.o.reconnectMinMs;
    this.timers = new Set();
  }

  start() { this.stopped = false; this.open(); return this; }

  stop() {
    this.stopped = true;
    for (const t of this.timers) clearTimeout(t);
    this.timers.clear();
    if (this.ws) try { this.ws.close(1000, "bridge stopping"); } catch {}
  }

  later(ms, fn) {
    const t = setTimeout(() => { this.timers.delete(t); fn(); }, ms);
    this.timers.add(t);
    return t;
  }

  nextId(prefix) { return `${prefix}-${Date.now()}-${++this.seq}`; }

  send(frame) {
    if (this.ws && this.ws.readyState === 1) this.ws.send(JSON.stringify(frame));
  }

  open() {
    if (this.stopped) return;
    this.ready = false;
    this.connectId = null;
    let ws;
    try { ws = new WebSocket(this.o.url); }
    catch (e) { this.log("error", `cannot open ${this.o.url}: ${e.message}`); return this.retry(); }
    this.ws = ws;
    const challengeTimer = this.later(this.o.challengeTimeoutMs, () => {
      if (ws === this.ws && !this.connectId) { this.log("warn", "no connect.challenge from the gateway"); ws.close(4000, "no challenge"); }
    });
    ws.addEventListener("message", (ev) => {
      if (ws !== this.ws) return;
      this.lastFrame = Date.now();
      let frame;
      try { frame = JSON.parse(typeof ev.data === "string" ? ev.data : Buffer.from(ev.data).toString("utf8")); }
      catch { return this.log("warn", "frame that is not JSON ignored"); }
      this.onFrame(frame, challengeTimer);
    });
    ws.addEventListener("close", (ev) => {
      if (ws !== this.ws) return;
      clearTimeout(challengeTimer);
      this.timers.delete(challengeTimer);
      const was = this.ready;
      this.ready = false;
      this.ws = null;
      this.emit("closed", { code: ev.code, reason: ev.reason, wasReady: was });
      this.retry();
    });
    ws.addEventListener("error", () => {});   // "close" follows, and carries the retry
  }

  retry() {
    if (this.stopped) return;
    const wait = this.backoff;
    this.backoff = Math.min(this.backoff * 2, this.o.reconnectMaxMs);
    this.later(wait, () => this.open());
  }

  onFrame(frame, challengeTimer) {
    if (frame.type === "event" && frame.event === "connect.challenge") {
      const { nonce, ts } = frame.payload || {};
      if (typeof nonce !== "string" || !Number.isInteger(ts) || ts < 0) {
        this.log("error", "malformed connect.challenge");
        return this.ws.close(4001, "bad challenge");
      }
      clearTimeout(challengeTimer);
      this.connectId = this.nextId("connect");
      this.send(connectRequest({
        id: this.connectId, identity: this.o.identity, nonce, signedAtMs: ts, auth: this.auth,
        client: this.o.client, commands: this.o.commands, caps: this.o.caps || [], locale: this.o.locale,
        userAgent: this.o.userAgent || `aam-openclaw-bridge/${this.o.client.version}`,
      }));
      return;
    }
    if (frame.type === "res" && frame.id && frame.id === this.connectId) return this.onConnectResponse(frame);
    if (frame.type === "event" && frame.event === "node.invoke.request" && this.ready) return this.onInvoke(frame.payload || {});
    if (frame.type === "event" && frame.event === "tick") return;
    if (frame.type === "res" && frame.ok === false) {
      this.log("warn", `gateway refused ${frame.id}: ${frame.error?.code || ""} ${frame.error?.message || ""}`.trim());
    }
  }

  onConnectResponse(frame) {
    if (frame.ok === true && frame.payload?.type === "hello-ok") {
      const token = frame.payload.auth?.deviceToken;
      if (typeof token === "string" && token.trim()) {
        // Only the node token is kept. A setup code also hands off an operator
        // token (payload.auth.deviceTokens); the bridge only shows things on the
        // clocks, so it never stores or uses one.
        if (!this.auth || this.auth.kind !== "deviceToken" || this.auth.value !== token) {
          this.auth = { kind: "deviceToken", value: token };
          if (this.o.onDeviceToken) this.o.onDeviceToken(token);
        }
      }
      this.ready = true;
      this.backoff = this.o.reconnectMinMs;
      this.emit("ready", frame.payload);
      return;
    }
    const err = frame.error || {};
    const retryAfter = err.details?.retryAfterMs;
    this.emit("rejected", err);
    if (err.code === "UNAVAILABLE" && Number.isFinite(retryAfter)) {
      this.log("info", `gateway starting; retrying in ${retryAfter} ms`);
      this.backoff = Math.max(this.o.reconnectMinMs, retryAfter);
    } else {
      // Bad credentials or a pairing still to approve: keep retrying slowly, so an
      // approval (`openclaw devices approve`) is picked up without a restart.
      this.log("error", `connect refused: ${err.code || "?"} ${err.message || ""}${err.details?.code ? ` (${err.details.code})` : ""}`);
      this.backoff = this.o.reconnectMaxMs;
    }
    if (this.ws) this.ws.close(1000, "connect refused");
  }

  async onInvoke(req) {
    const { id, nodeId, command } = req;
    if (typeof id !== "string" || typeof command !== "string") return;
    let params = {};
    let result;
    try {
      if (req.paramsJSON !== undefined && req.paramsJSON !== null && req.paramsJSON !== "") {
        try { params = JSON.parse(req.paramsJSON); }
        catch { throw new InvokeError("INVALID_PARAMS", "paramsJSON is not JSON"); }
      }
      if (params === null || typeof params !== "object" || Array.isArray(params)) throw new InvokeError("INVALID_PARAMS", "params must be an object");
      const budget = Math.min(this.o.invokeTimeoutMs, Number.isInteger(req.timeoutMs) && req.timeoutMs > 0 ? req.timeoutMs : Infinity);
      let timer;
      const payload = await Promise.race([
        Promise.resolve().then(() => this.o.handle(command, params, { id, nodeId, idempotencyKey: req.idempotencyKey })),
        new Promise((_, reject) => { timer = this.later(budget, () => reject(new InvokeError("TIMEOUT", `the clock did not answer within ${budget} ms`))); }),
      ]).finally(() => { clearTimeout(timer); this.timers.delete(timer); });
      result = { id, nodeId, ok: true, payloadJSON: JSON.stringify(payload ?? {}) };
    } catch (e) {
      const code = e instanceof InvokeError ? e.code : "UNAVAILABLE";
      result = { id, nodeId, ok: false, error: { code, message: String(e.message || e).slice(0, 500) || code } };
      this.log("warn", `${command} failed: ${code} ${e.message}`);
    }
    this.send({ type: "req", id: this.nextId("invoke-result"), method: "node.invoke.result", params: result });
  }
}
