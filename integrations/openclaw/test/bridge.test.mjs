// Bridge tests: the protocol pieces, then whole round trips through a fake
// gateway and a fake clock. `node --test test/` (no packages). The same round
// trips against the real firmware in the Docker emulator: test/emulator.test.mjs.

import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, existsSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createHash } from "node:crypto";
import { authPayloadV3, connectRequest, decodeSetupCode, makeIdentity, normalizeMeta } from "../lib/protocol.mjs";
import { COMMAND_NAMES, helpLinks } from "../lib/commands.mjs";
import { loadConfig, startClock } from "../bridge.mjs";
import { FakeGateway, buildDeviceAuthPayloadV3 } from "./fake-gateway.mjs";
import { FakeClock } from "./fake-clock.mjs";

const quiet = () => {};

// ---------------------------------------------------------------- protocol pieces
test("identity: device id is sha256 of the raw public key, key is base64url without padding", () => {
  const seed = Buffer.alloc(32, 7);
  const a = makeIdentity(seed), b = makeIdentity(seed);
  assert.equal(a.deviceId, b.deviceId);
  assert.equal(a.publicKey, b.publicKey);
  assert.match(a.publicKey, /^[A-Za-z0-9_-]{43}$/);
  assert.equal(a.deviceId, createHash("sha256").update(Buffer.from(a.publicKey, "base64url")).digest("hex"));
  assert.notEqual(makeIdentity().deviceId, a.deviceId);
});

test("identity: RFC 8032 test vector 1 (seed -> public key)", () => {
  const seed = Buffer.from("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", "hex");
  const pub = Buffer.from(makeIdentity(seed).publicKey, "base64url").toString("hex");
  assert.equal(pub, "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
});

test("v3 payload matches the gateway's builder and the ESP32 node's format", () => {
  const p = { deviceId: "abc", clientId: "node-host", clientMode: "node", role: "node", scopes: [], signedAtMs: 1737264000000,
    token: "tok", nonce: "n1", platform: " ESP8266 ", deviceFamily: "AI Agent Monitor" };
  const ours = authPayloadV3(p);
  assert.equal(ours, buildDeviceAuthPayloadV3(p));
  // esp_openclaw_node_identity.c: "v3|%s|%s|%s|%s|%s|%" PRId64 "|%s|%s|%s|%s"
  assert.equal(ours, "v3|abc|node-host|node|node||1737264000000|tok|n1|esp8266|ai agent monitor");
  assert.equal(authPayloadV3({ ...p, token: undefined }).split("|")[7], "");
  assert.equal(normalizeMeta(undefined), "");
});

test("connect request: node role, protocol 3-4, signed, password kept out of the signature", () => {
  const identity = makeIdentity(Buffer.alloc(32, 1));
  const base = { id: "c1", identity, nonce: "nn", signedAtMs: 5, client: { platform: "esp8266", deviceFamily: "x", version: "0" }, commands: COMMAND_NAMES };
  const r = connectRequest({ ...base, auth: { kind: "bootstrapToken", value: "bt" } });
  assert.equal(r.method, "connect");
  assert.deepEqual([r.params.minProtocol, r.params.maxProtocol, r.params.role, r.params.client.id, r.params.client.mode], [3, 4, "node", "node-host", "node"]);
  assert.deepEqual(r.params.auth, { bootstrapToken: "bt" });
  assert.deepEqual(r.params.scopes, []);
  assert.equal(r.params.device.signedAt, 5);
  const withPw = connectRequest({ ...base, auth: { kind: "password", value: "pw" } });
  const noAuth = connectRequest({ ...base, auth: null });
  assert.equal(withPw.params.device.signature, noAuth.params.device.signature);   // same signed bytes: token ""
  assert.equal(noAuth.params.auth, undefined);
});

test("setup code: decodes {url, one credential}; refuses the rest", () => {
  const code = Buffer.from(JSON.stringify({ url: "ws://192.168.1.10:18789", bootstrapToken: "oc_bt" })).toString("base64url");
  assert.deepEqual(decodeSetupCode(code), { url: "ws://192.168.1.10:18789", auth: { kind: "bootstrapToken", value: "oc_bt" } });
  const two = Buffer.from(JSON.stringify({ url: "ws://x", token: "a", password: "b" })).toString("base64url");
  assert.throws(() => decodeSetupCode(two), /exactly one/);
  assert.throws(() => decodeSetupCode("%%%"), /setup code/);
});

// ---------------------------------------------------------------- round trips
async function rig({ gateway = {}, clock = {}, config = {} } = {}) {
  const gw = await new FakeGateway(gateway).listen();
  const fc = await new FakeClock(clock).listen();
  const dir = mkdtempSync(join(tmpdir(), "aam-oc-"));
  const file = join(dir, "clocks.json");
  writeFileSync(file, JSON.stringify({
    clocks: [{ name: "desk", url: fc.url, apiKey: fc.apiKey }],
    ...config,
    gateway: { url: gw.url, token: "gw-shared-token", ...config.gateway },
  }));
  const cfg = loadConfig(file);
  const running = [];
  const start = (opts = {}) => { const r = startClock(cfg, cfg.clocks[0], { log: quiet, connectionOptions: { reconnectMinMs: 50, reconnectMaxMs: 200, ...opts } }); running.push(r); return r; };
  const close = async () => { for (const r of running) r.conn.stop(); await gw.close(); await fc.close(); };
  return { gw, fc, cfg, dir, start, close };
}

const ready = (conn) => new Promise((ok) => conn.once("ready", ok));

test("pairs with the gateway token, keeps only the node token, declares the commands", async (t) => {
  const r = await rig();
  t.after(r.close);
  const { conn, identity } = r.start();
  await ready(conn);
  const state = JSON.parse(readFileSync(join(r.dir, "state", "desk.json"), "utf8"));
  assert.equal(state.deviceToken, r.gw.o.deviceToken);
  assert.ok(!JSON.stringify(state).includes("operator-handoff-token"), "operator handoff token must not be stored");
  const p = r.gw.connects.at(-1);
  assert.deepEqual(p.commands, COMMAND_NAMES);
  assert.equal(p.client.platform, "esp8266");
  assert.ok(r.gw.nodes.has(identity.deviceId));
});

test("display.show posts a message; display.status and display.clear follow it", async (t) => {
  const r = await rig();
  t.after(r.close);
  const { conn, identity } = r.start();
  await ready(conn);
  const res = await r.gw.invoke(identity.deviceId, "display.show", { heading: "OpenClaw", text: "Hello from the gateway.", icon: "waiting" });
  assert.equal(res.ok, true, JSON.stringify(res.error));
  assert.deepEqual(res.payload, { clock: "desk", id: "openclaw", shown: true, clipped: false });
  assert.deepEqual(r.fc.messages.get("openclaw"), { id: "openclaw", title: "OpenClaw", text: "Hello from the gateway.", show: true, icon: "waiting" });
  const st = await r.gw.invoke(identity.deviceId, "display.status", {});
  assert.equal(st.payload.face, 15);
  assert.equal(st.payload.messages[0].id, "openclaw");
  const cl = await r.gw.invoke(identity.deviceId, "display.clear", {});
  assert.deepEqual(cl.payload.cleared, ["openclaw", "mood"]);
  assert.equal(r.fc.messages.size, 0);
});

test("display.show clips to the clock's limits and refuses bad params", async (t) => {
  const r = await rig();
  t.after(r.close);
  const { conn, identity } = r.start();
  await ready(conn);
  const long = await r.gw.invoke(identity.deviceId, "display.show", { heading: "H".repeat(60), text: "t".repeat(300) });
  assert.equal(long.payload.clipped, true);
  assert.equal(r.fc.messages.get("openclaw").title.length, 40);
  assert.equal(r.fc.messages.get("openclaw").text.length, 160);
  for (const [params, re] of [
    [{ heading: "x" }, /text is required/],
    [{ text: "x", icon: "rocket" }, /icon must be one of/],
    [{ text: "x", pct: 101 }, /pct must be/],
    [{ text: "x", colour: "#fff" }, /does not take colour/],
    [{ text: "x", id: "has space" }, /id may only/],
  ]) {
    const bad = await r.gw.invoke(identity.deviceId, "display.show", params);
    assert.equal(bad.ok, false);
    assert.equal(bad.error.code, "INVALID_PARAMS");
    assert.match(bad.error.message, re);
  }
  const notJson = await r.gw.invoke(identity.deviceId, "display.show", null, { raw: "{nope" });
  assert.equal(notJson.error.code, "INVALID_PARAMS");
  const unknown = await r.gw.invoke(identity.deviceId, "camera.snap", {});
  assert.equal(unknown.error.code, "UNKNOWN_COMMAND");
});

test("face.set: moods with Moni post a message, neutral clears it", async (t) => {
  const r = await rig();
  t.after(r.close);
  const { conn, identity } = r.start();
  await ready(conn);
  const happy = await r.gw.invoke(identity.deviceId, "face.set", { mood: "happy", text: "Build passed" });
  assert.deepEqual(happy.payload, { clock: "desk", mood: "happy", moni: true, icon: "done" });
  assert.equal(r.fc.messages.get("mood").icon, "done");
  const thinking = await r.gw.invoke(identity.deviceId, "face.set", { mood: "thinking" });
  assert.equal(thinking.payload.icon, "waiting");
  await r.gw.invoke(identity.deviceId, "face.set", { mood: "neutral" });
  assert.equal(r.fc.messages.has("mood"), false);
  const bad = await r.gw.invoke(identity.deviceId, "face.set", { mood: "angry" });
  assert.match(bad.error.message, /mood must be one of/);
});

test("brightness and built-in faces need the Home Assistant integration; custom faces need the key", async (t) => {
  const r = await rig();
  t.after(r.close);
  const { conn, identity } = r.start();
  await ready(conn);
  const off = await r.gw.invoke(identity.deviceId, "display.brightness", { percent: 30 });
  assert.equal(off.error.code, "NOT_CONFIGURED");
  assert.match(off.error.message, /Home Assistant dashboards/);
  r.fc.haCompat = true;
  assert.equal((await r.gw.invoke(identity.deviceId, "display.brightness", { percent: 30 })).ok, true);
  assert.equal(r.fc.brightness, 30);
  assert.equal((await r.gw.invoke(identity.deviceId, "clock.face.show", { builtin: "big-time" })).ok, true);
  assert.equal(r.fc.face, 7);
  assert.equal((await r.gw.invoke(identity.deviceId, "clock.face.show", { builtin: "day-4" })).ok, true);   // id 6, the Day 4 tile
  assert.equal(r.fc.face, 6);
  assert.equal((await r.gw.invoke(identity.deviceId, "clock.face.show", { builtin: 14 })).error.code, "INVALID_PARAMS");   // custom, not built in
  const face = { bg: "#000", items: [{ type: "text", x: 10, y: 10, text: "{build}" }] };
  const saved = await r.gw.invoke(identity.deviceId, "clock.face.save", { name: "build", face, show: true });
  assert.equal(saved.ok, true, JSON.stringify(saved.error));
  assert.equal(r.fc.custom, "build");
  const vars = await r.gw.invoke(identity.deviceId, "clock.vars.set", { vars: { build: "green", pct: 62 }, ttl: 900, history: true });
  assert.equal(vars.ok, true);
  assert.deepEqual(r.fc.vars, { build: "green", pct: 62, $ttl: 900, $history: true });
  const list = await r.gw.invoke(identity.deviceId, "clock.faces.list", {});
  assert.deepEqual(list.payload.faces, ["build"]);
  assert.equal((await r.gw.invoke(identity.deviceId, "clock.vars.set", { vars: { "bad.name": 1 } })).error.code, "INVALID_PARAMS");
  r.fc.apiKey = "another-key-entirely-123";
  const refused = await r.gw.invoke(identity.deviceId, "clock.vars.set", { vars: { a: 1 } });
  assert.equal(refused.error.code, "NOT_CONFIGURED");
});

test("device.info, device.status and clock.help are read-only summaries of /status", async (t) => {
  const r = await rig();
  t.after(r.close);
  const { conn, identity } = r.start();
  await ready(conn);
  const before = r.fc.requests.length;
  const info = await r.gw.invoke(identity.deviceId, "device.info", {});
  assert.deepEqual(info.payload, { clock: "desk", model: "SD PRO", platform: "esp8266", firmware: "2.2.0", build: "fake", host: "agentmonitor-fake" });
  const st = await r.gw.invoke(identity.deviceId, "device.status", {});
  assert.equal(st.payload.rssi, -55);
  const help = await r.gw.invoke(identity.deviceId, "clock.help", {});
  assert.deepEqual(help.payload, { clock: "desk", firmware: "2.2.0",
    skill: "https://github.com/SimonLuckenuik/ai-agent-monitor/tree/v2.2.0/integrations/openclaw/skill/ai-agent-monitor",
    guide: "https://raw.githubusercontent.com/SimonLuckenuik/ai-agent-monitor/v2.2.0/integrations/openclaw/skill/ai-agent-monitor/SKILL.md",
    install: "openclaw skills install <skill folder> --global" });
  assert.equal((await r.gw.invoke(identity.deviceId, "clock.help", { x: 1 })).error.code, "INVALID_PARAMS");
  assert.equal(helpLinks("2.4.0-dev").guide.includes("/main/"), true);
  assert.ok(r.fc.requests.slice(before).every((q) => q.method === "GET"));
});

test("clock.catalog returns the json-render prompt; clock.face.save takes a spec up to 12 KB", async (t) => {
  const r = await rig();
  t.after(r.close);
  const { conn, identity } = r.start();
  await ready(conn);
  const none = await r.gw.invoke(identity.deviceId, "clock.catalog", {});
  assert.equal(none.error.code, "UNAVAILABLE");
  assert.match(none.error.message, /no json-render/);
  r.fc.catalog = "# Drawing on the AI Agent Monitor clock with json-render\n";
  const cat = await r.gw.invoke(identity.deviceId, "clock.catalog", {});
  assert.equal(cat.ok, true, JSON.stringify(cat.error));
  assert.deepEqual(cat.payload, { clock: "desk", prompt: "# Drawing on the AI Agent Monitor clock with json-render" });
  assert.equal((await r.gw.invoke(identity.deviceId, "clock.catalog", { x: 1 })).error.code, "INVALID_PARAMS");
  const pad = "x".repeat(6000);
  const spec = { root: "t", elements: { t: { type: "Text", props: { text: pad }, children: [] } } };
  const saved = await r.gw.invoke(identity.deviceId, "clock.face.save", { name: "spec", face: spec });
  assert.equal(saved.ok, true, JSON.stringify(saved.error));
  const face = { bg: "#000", items: [{ type: "text", x: 0, y: 0, text: pad }] };
  assert.equal((await r.gw.invoke(identity.deviceId, "clock.face.save", { name: "big", face })).error.code, "INVALID_PARAMS");
});

test("a clock that does not answer gives UNAVAILABLE, and the node stays connected", async (t) => {
  const r = await rig();
  t.after(r.close);
  const { conn, identity } = r.start();
  await ready(conn);
  await r.fc.close();
  const res = await r.gw.invoke(identity.deviceId, "display.show", { text: "x" });
  assert.equal(res.error.code, "UNAVAILABLE");
  assert.ok(r.gw.nodes.has(identity.deviceId));
});

test("setup code: bootstrap token used once, then the saved device token on reconnect", async (t) => {
  const r = await rig({ gateway: { bootstrapToken: "oc_bootstrap_1" }, config: { gateway: { token: undefined } } });
  t.after(r.close);
  // what `bridge.mjs pair desk <code>` stores
  const code = Buffer.from(JSON.stringify({ url: r.gw.url, bootstrapToken: "oc_bootstrap_1" })).toString("base64url");
  const statePath = join(r.dir, "state", "desk.json");
  mkdirSync(join(r.dir, "state"), { recursive: true });
  writeFileSync(statePath, JSON.stringify({ setup: decodeSetupCode(code) }));
  const { conn, identity } = r.start();
  await ready(conn);
  assert.equal(r.gw.connects.at(-1).auth.bootstrapToken, "oc_bootstrap_1");
  const state = JSON.parse(readFileSync(statePath, "utf8"));
  assert.equal(state.setup, undefined, "bootstrap token forgotten once used");
  assert.equal(state.deviceToken, r.gw.o.deviceToken);
  r.gw.drop(identity.deviceId);
  await ready(conn);
  assert.equal(r.gw.connects.at(-1).auth.deviceToken, r.gw.o.deviceToken);
  assert.equal((await r.gw.invoke(identity.deviceId, "display.show", { text: "after reconnect" })).ok, true);
});

test("pairing to approve: the bridge keeps retrying and connects once approved", async (t) => {
  const r = await rig({ gateway: { requirePairing: true } });
  t.after(r.close);
  const { conn, identity } = r.start();
  await r.gw.waitFor("pairing");
  assert.equal(conn.ready, false);
  r.gw.approve(identity.deviceId);
  await ready(conn);
  assert.equal(existsSync(join(r.dir, "state", "desk.json")), true);
});

test("a refused credential is not retried fast (backs off to the maximum)", async (t) => {
  const r = await rig({ config: { gateway: { token: "wrong-token" } } });
  t.after(r.close);
  const { conn } = r.start({ reconnectMaxMs: 400 });
  await new Promise((ok) => conn.once("rejected", ok));
  const n = r.gw.connects.length;
  await new Promise((ok) => setTimeout(ok, 250));
  assert.equal(r.gw.connects.length, n, "no new attempt before the back-off");
});

test("the fake gateway refuses a forged signature (so the passing tests mean something)", async (t) => {
  const gw = await new FakeGateway().listen();
  t.after(() => gw.close());
  const ws = new WebSocket(gw.url);
  const frames = [];
  const next = () => new Promise((ok) => { const f = () => { if (frames.length) ok(frames.shift()); else setTimeout(f, 5); }; f(); });
  ws.addEventListener("message", (ev) => frames.push(JSON.parse(ev.data)));
  const challenge = await next();
  assert.equal(challenge.event, "connect.challenge");
  const real = makeIdentity(Buffer.alloc(32, 2)), other = makeIdentity(Buffer.alloc(32, 3));
  const req = connectRequest({ id: "c1", identity: real, nonce: challenge.payload.nonce, signedAtMs: challenge.payload.ts,
    auth: { kind: "token", value: "gw-shared-token" }, client: { platform: "esp8266", deviceFamily: "x", version: "0" }, commands: [] });
  req.params.device.signature = connectRequest({ ...req, id: "c1", identity: other, nonce: challenge.payload.nonce, signedAtMs: challenge.payload.ts,
    auth: { kind: "token", value: "gw-shared-token" }, client: { platform: "esp8266", deviceFamily: "x", version: "0" }, commands: [] }).params.device.signature;
  ws.send(JSON.stringify(req));
  const res = await next();
  assert.equal(res.ok, false);
  assert.match(res.error.message, /signature/);
  ws.close();
});
