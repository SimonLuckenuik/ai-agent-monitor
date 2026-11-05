// The bridge against the real firmware in the Docker emulator, through the fake
// gateway. Skipped unless AAM_EMU_API is set:
//
//   docker build -f emulator/Dockerfile -t aam-openclaw-emu .
//   docker run -d --name aam-openclaw-emu -p 127.0.0.1:28000:80 -p 127.0.0.1:28080:8080 aam-openclaw-emu
//   AAM_EMU_API=http://127.0.0.1:28000 AAM_EMU_SCREEN=http://127.0.0.1:28080 AAM_SHOTS=out node --test test/emulator.test.mjs
//
// It signs in to the emulator (its password is on the screen server: emulator
// only), makes a faces API key, and turns on the faces API and the Home Assistant
// integration. AAM_SHOTS saves the screen after each step as PNG.

import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, mkdirSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { deflateSync, crc32 } from "node:zlib";
import { loadConfig, startClock } from "../bridge.mjs";
import { FakeGateway } from "./fake-gateway.mjs";

const API = process.env.AAM_EMU_API;
const SCREEN = process.env.AAM_EMU_SCREEN || (API || "").replace(/:\d+$/, ":28080");
const SHOTS = process.env.AAM_SHOTS;
const sleep = (ms) => new Promise((ok) => setTimeout(ok, ms));

async function signIn() {
  const password = (await (await fetch(SCREEN + "/password")).text()).trim();
  const res = await fetch(API + "/api/auth/login", { method: "POST", body: new URLSearchParams({ password }) });
  assert.equal(res.status, 200, "emulator sign-in");
  return res.headers.get("set-cookie").split(";")[0];
}
async function set(cookie, key, value) {
  const res = await fetch(API + "/api/set", { method: "POST", headers: { Cookie: cookie }, body: new URLSearchParams({ key, value: String(value) }) });
  assert.equal(await res.text(), "OK", `set ${key}`);
}

// ---- screenshots: the screen server's RGB565 frame buffer, as PNG
function png(raw) {
  const rows = [];
  for (let y = 0; y < 240; y++) {
    const row = Buffer.alloc(1 + 240 * 3);
    for (let x = 0; x < 240; x++) {
      const v = raw[(y * 240 + x) * 2] | (raw[(y * 240 + x) * 2 + 1] << 8);
      row[1 + x * 3] = ((v >> 11) * 255 / 31) | 0;
      row[2 + x * 3] = (((v >> 5) & 63) * 255 / 63) | 0;
      row[3 + x * 3] = ((v & 31) * 255 / 31) | 0;
    }
    rows.push(row);
  }
  const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const body = Buffer.concat([Buffer.from(type), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(body));
    return Buffer.concat([len, body, crc]);
  };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(240, 0); ihdr.writeUInt32BE(240, 4); ihdr[8] = 8; ihdr[9] = 2;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk("IHDR", ihdr), chunk("IDAT", deflateSync(Buffer.concat(rows))), chunk("IEND", Buffer.alloc(0))]);
}
async function shot(name, wait = 1500) {
  if (!SHOTS) return;
  await sleep(wait);
  const raw = Buffer.from(await (await fetch(SCREEN + "/fb")).arrayBuffer());
  mkdirSync(SHOTS, { recursive: true });
  writeFileSync(join(SHOTS, `${name}.png`), png(raw));
}

test("the bridge drives the firmware in the emulator", { skip: !API && "set AAM_EMU_API to run against the emulator" }, async (t) => {
  const cookie = await signIn();
  const keyRes = await fetch(API + "/api/integrations", { method: "POST", headers: { Cookie: cookie }, body: new URLSearchParams({ action: "newkey" }) });
  const apiKey = (await keyRes.json()).apiKey;
  await set(cookie, "facesapi", 1);
  await set(cookie, "hacompat", 1);
  await set(cookie, "nightmode", 0);   // no night hours: Moni is not drawn at night
  await set(cookie, "nightauto", 0);
  await fetch(API + "/api/message", { method: "DELETE" });

  const gw = await new FakeGateway().listen();
  const dir = mkdtempSync(join(tmpdir(), "aam-oc-emu-"));
  writeFileSync(join(dir, "clocks.json"), JSON.stringify({ gateway: { url: gw.url, token: "gw-shared-token" }, clocks: [{ name: "emu", url: API, apiKey }] }));
  const cfg = loadConfig(join(dir, "clocks.json"));
  const { conn, identity } = startClock(cfg, cfg.clocks[0], { log: () => {} });
  t.after(async () => { conn.stop(); await gw.close(); });
  await new Promise((ok) => conn.once("ready", ok));
  const call = async (command, params) => {
    const r = await gw.invoke(identity.deviceId, command, params);
    assert.equal(r.ok, true, `${command}: ${JSON.stringify(r.error)}`);
    return r.payload;
  };

  const info = await call("device.info", {});
  assert.match(info.firmware, /^\d+\.\d+\.\d+/);

  await call("display.show", { heading: "OpenClaw", text: "Waiting for your approval to deploy.", icon: "waiting" });
  let msgs = (await (await fetch(API + "/api/message")).json()).messages;
  assert.equal(msgs.find((m) => m.id === "openclaw")?.icon, "waiting");
  assert.equal((await call("display.status", {})).face, 15);
  await shot("1-display-show-waiting");

  await call("face.set", { mood: "happy", text: "Build passed" });
  await shot("2-face-set-happy");
  await call("face.set", { mood: "sad", text: "Tests failed" });
  await shot("3-face-set-sad", 5000);

  await call("display.clear", {});
  msgs = (await (await fetch(API + "/api/message")).json()).messages;
  assert.equal(msgs.filter((m) => m.id === "openclaw" || m.id === "mood").length, 0);

  const face = { bg: "#001020", items: [
    { type: "text", x: 120, y: 60, text: "OpenClaw", font: 4, color: "#64d2ff", align: "tc" },
    { type: "text", x: 120, y: 120, text: "{task|idle}", font: "big", size: 32, color: "#ffffff", align: "tc" },
    { type: "text", x: 120, y: 190, text: "{pct|0}%", font: 4, color: "#30d158", align: "tc" },
  ] };
  await call("clock.face.save", { name: "openclaw", face, show: true });
  await call("clock.vars.set", { vars: { task: "deploy", pct: 62 }, ttl: 900 });
  const st = await (await fetch(API + "/status")).json();
  assert.equal(st.customFace, "openclaw");
  assert.deepEqual((await call("clock.faces.list", {})).faces.includes("openclaw"), true);
  await shot("4-custom-face-vars");

  await call("display.brightness", { percent: 40 });
  await call("clock.face.show", { builtin: "big-time" });
  assert.equal((await (await fetch(API + "/status")).json()).face, 7);
  await shot("5-builtin-big-time");

  const health = await call("device.status", {});
  assert.ok(health.heap > 0);
});
