#!/usr/bin/env node
// AI Agent Monitor <-> OpenClaw bridge: pairs each configured clock with an
// OpenClaw Gateway as a node of its own, and turns the commands agents send it
// into the clock's HTTP API. Screen output and read-only status only (spec 041).
//
//   node bridge.mjs [--config clocks.json]          run (all clocks)
//   node bridge.mjs pair <clock> <setup-code>       pair one clock with a setup code, then run
//   node bridge.mjs info [--config clocks.json]     device ids, pairing state, the allow list to add
//
// Node 22 or later; no packages to install.

import { chmodSync, existsSync, mkdirSync, readFileSync, renameSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { NodeConnection, decodeSetupCode, makeIdentity, InvokeError } from "./lib/protocol.mjs";
import { Clock } from "./lib/clock.mjs";
import { commandsFor, COMMAND_NAMES } from "./lib/commands.mjs";

const HERE = dirname(fileURLToPath(import.meta.url));
export const VERSION = JSON.parse(readFileSync(join(HERE, "package.json"), "utf8")).version;

/** "env:NAME" reads the environment, so secrets can stay out of the file. */
const secret = (v) => (typeof v === "string" && v.startsWith("env:") ? process.env[v.slice(4)] || "" : v || "");

export function loadConfig(path) {
  const file = resolve(path);
  const cfg = JSON.parse(readFileSync(file, "utf8"));
  if (!Array.isArray(cfg.clocks) || !cfg.clocks.length) throw new Error(`${file}: "clocks" must list at least one clock`);
  const names = new Set();
  for (const c of cfg.clocks) {
    if (!/^[a-z0-9-]{1,32}$/.test(c.name || "")) throw new Error(`clock name ${JSON.stringify(c.name)}: 1-32 of a-z 0-9 -`);
    if (names.has(c.name)) throw new Error(`clock ${c.name} is listed twice`);
    names.add(c.name);
  }
  return {
    gatewayUrl: cfg.gateway?.url || "",
    gatewayAuth: cfg.gateway?.token ? { kind: "token", value: secret(cfg.gateway.token) }
      : cfg.gateway?.password ? { kind: "password", value: secret(cfg.gateway.password) } : null,
    stateDir: resolve(dirname(file), cfg.stateDir || "state"),
    clocks: cfg.clocks.map((c) => ({ ...c, apiKey: secret(c.apiKey) })),
  };
}

// ---------------------------------------------------------------- per-clock state (keys, tokens)
export class State {
  constructor(dir, name) { this.path = join(dir, `${name}.json`); this.data = existsSync(this.path) ? JSON.parse(readFileSync(this.path, "utf8")) : {}; }
  save() {
    mkdirSync(dirname(this.path), { recursive: true });
    const tmp = this.path + ".tmp";
    writeFileSync(tmp, JSON.stringify(this.data, null, 2) + "\n", { mode: 0o600 });
    renameSync(tmp, this.path);
    try { chmodSync(this.path, 0o600); } catch {}
  }
  identity() {
    if (!this.data.seed) { this.data.seed = makeIdentity().seed.toString("base64url"); this.save(); }
    return makeIdentity(Buffer.from(this.data.seed, "base64url"));
  }
}

/** What a clock connects with: its saved device token, a pending setup code, or the gateway's shared secret. */
export function connectPlan(cfg, state) {
  if (state.data.deviceToken) return { url: state.data.gatewayUrl || cfg.gatewayUrl, auth: { kind: "deviceToken", value: state.data.deviceToken } };
  if (state.data.setup) return { url: state.data.setup.url, auth: state.data.setup.auth };
  return { url: cfg.gatewayUrl, auth: cfg.gatewayAuth };
}

export function startClock(cfg, clockCfg, { log = console.log, connectionOptions = {} } = {}) {
  const state = new State(cfg.stateDir, clockCfg.name);
  const identity = state.identity();
  const plan = connectPlan(cfg, state);
  const say = (level, msg) => log(`[${clockCfg.name}] ${level}: ${msg}`);
  if (!plan.url) throw new Error(`clock ${clockCfg.name}: no gateway url (config gateway.url, or pair with a setup code)`);
  const clock = new Clock(clockCfg);
  const table = commandsFor(clock, { messageId: clockCfg.messageId, defaultHeading: clockCfg.heading });
  const conn = new NodeConnection({
    url: plan.url,
    identity,
    auth: plan.auth,
    client: {
      displayName: clockCfg.displayName || `AI Agent Monitor ${clockCfg.name}`,
      version: VERSION,
      platform: "esp8266",
      deviceFamily: "AI Agent Monitor",
      modelIdentifier: "SD PRO",
    },
    commands: COMMAND_NAMES,
    log: say,
    ...connectionOptions,
    async handle(command, params) {
      const entry = table[command];
      if (!entry) throw new InvokeError("UNKNOWN_COMMAND", `${command} is not a command of this clock`);
      return entry.run(params);
    },
    onDeviceToken(token) {
      state.data.deviceToken = token;
      state.data.gatewayUrl = plan.url;
      delete state.data.setup;   // a setup code's bootstrap token is single use: never kept
      state.save();
      say("info", "paired: device token saved");
    },
  });
  conn.on("ready", (hello) => say("info", `connected to the gateway (protocol ${hello.protocol}) as ${identity.deviceId.slice(0, 12)}...`));
  conn.on("closed", ({ code, reason, wasReady }) => { if (wasReady) say("warn", `disconnected (${code} ${reason || ""}); reconnecting`); });
  conn.on("rejected", (err) => {
    // A device token the gateway no longer knows (node removed): forget it, so
    // the next try uses the shared secret and asks for pairing again.
    if (plan.auth?.kind === "deviceToken" && /token|auth|pair/i.test(`${err.code} ${err.details?.code || ""}`) && cfg.gatewayAuth) {
      say("warn", "saved device token refused; falling back to the gateway token (approve the pairing again)");
      delete state.data.deviceToken;
      state.save();
      conn.auth = cfg.gatewayAuth;
      plan.auth = cfg.gatewayAuth;
    }
  });
  return { conn: conn.start(), identity, state, clock };
}

function info(cfg) {
  for (const c of cfg.clocks) {
    const state = new State(cfg.stateDir, c.name);
    const id = state.identity();
    const how = state.data.deviceToken ? "paired (device token saved)" : state.data.setup ? "setup code waiting to be used" : cfg.gatewayAuth ? "not paired yet: will ask with the gateway token" : "not paired: run `pair` with a setup code";
    console.log(`${c.name}  ${c.url}\n  device id ${id.deviceId}\n  ${how}`);
  }
  console.log(`\nAdd to the gateway's openclaw.json (custom node commands are not allowed by default):\n`);
  console.log(JSON.stringify({ gateway: { nodes: { commands: { allow: COMMAND_NAMES } } } }, null, 2));
}

async function main(argv) {
  let configPath = join(HERE, "clocks.json");
  const args = [];
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === "--config") configPath = argv[++i];
    else if (argv[i] === "-h" || argv[i] === "--help") { console.log(readFileSync(fileURLToPath(import.meta.url), "utf8").split("\n").slice(1, 9).map((l) => l.replace(/^\/\/ ?/, "")).join("\n")); return; }
    else args.push(argv[i]);
  }
  const cfg = loadConfig(configPath);
  const [cmd = "run", ...rest] = args;
  if (cmd === "info") return info(cfg);
  if (cmd === "pair") {
    const [name, code] = rest;
    const c = cfg.clocks.find((x) => x.name === name);
    if (!c || !code) throw new Error("usage: bridge.mjs pair <clock> <setup-code>");
    const state = new State(cfg.stateDir, name);
    state.data.setup = decodeSetupCode(code);
    delete state.data.deviceToken;
    state.save();
    console.log(`[${name}] setup code stored for ${state.data.setup.url}; connecting`);
    startClock(cfg, c);
    return;
  }
  if (cmd !== "run") throw new Error(`unknown command ${cmd} (run, pair, info)`);
  const running = cfg.clocks.map((c) => startClock(cfg, c));
  const stop = () => { for (const r of running) r.conn.stop(); process.exit(0); };
  process.on("SIGINT", stop);
  process.on("SIGTERM", stop);
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main(process.argv.slice(2)).catch((e) => { console.error(e.message); process.exit(1); });
}
