// The node commands a clock offers, and how each becomes a call to its HTTP API.
// Output only (spec 041, the owner's decision of 2026-11-05): things shown on the
// screen, plus read-only status. Names follow OpenClaw's where one exists
// (display.*, face.set, device.*: the ESP32 nodes' commands); clock.* are the
// clock's own (faces and variables).

import { InvokeError } from "./protocol.mjs";

const ICONS = ["info", "success", "warning", "error", "waiting", "running", "bell", "done"];

// Built-in faces by name (face ids are an API: CLAUDE.md, "Face ids are an API").
export const BUILTIN_FACES = {
  classic: 0, weather: 1, photo: 2, "big-date": 3, simple: 4, forecast: 5,
  "big-time": 7, "big-weather": 8, agents: 9, today: 10, "day-1": 11, tomorrow: 11, "day-2": 12, "day-3": 13, "day-4": 6,
};

// OpenClaw's moods (the ESP32 dial's face.set) on the clock: Moni, the Message
// face's robot, shows for three icons only (spec 039). The others clear the mood.
export const MOODS = {
  happy:    { icon: "done",    title: "Done" },     // Moni hops, then keeps its arms up
  excited:  { icon: "done",    title: "Done" },
  thinking: { icon: "waiting", title: "Waiting" },  // Moni waves (attention, waiting for you)
  sad:      { icon: "error",   title: "Problem" },  // Moni slumps; the screen flashes
  neutral:  null,                                   // back to the clock's own faces
  sleepy:   null,
};

const fail = (msg) => { throw new InvokeError("INVALID_PARAMS", msg); };

function only(params, allowed, command) {
  const extra = Object.keys(params).filter((k) => !allowed.includes(k));
  if (extra.length) fail(`${command} does not take ${extra.join(", ")} (it takes ${allowed.join(", ") || "nothing"})`);
}
function str(params, key, { required = false, max = 1000 } = {}) {
  const v = params[key];
  if (v === undefined || v === null) { if (required) fail(`${key} is required`); return undefined; }
  if (typeof v !== "string") fail(`${key} must be a string`);
  if (v.length > max) fail(`${key} is longer than ${max} characters`);
  return v;
}
function int(params, key, lo, hi) {
  const v = params[key];
  if (v === undefined || v === null) return undefined;
  if (!Number.isInteger(v) || v < lo || v > hi) fail(`${key} must be an integer from ${lo} to ${hi}`);
  return v;
}
function bool(params, key) {
  const v = params[key];
  if (v === undefined || v === null) return undefined;
  if (typeof v !== "boolean") fail(`${key} must be true or false`);
  return v;
}
const clip = (s, n) => (s.length > n ? s.slice(0, n - 1) + "~" : s);   // the clock's limits: title 40, text 160

// clock.help's answer (spec 045), the firmware's own: links to the skill of the release the
// clock runs (a -dev version has no tag: main).
export function helpLinks(version) {
  const v = String(version || "");
  const ref = v && !v.includes("-") ? `v${v}` : "main";
  const repo = "SimonLuckenuik/ai-agent-monitor/";
  const tail = `${ref}/integrations/openclaw/skill/ai-agent-monitor`;
  return {
    firmware: v,
    skill: `https://github.com/${repo}tree/${tail}`,
    guide: `https://raw.githubusercontent.com/${repo}${tail}/SKILL.md`,
    install: "openclaw skills install <skill folder> --global",
  };
}

/** The command table for one clock. `opts.messageId` is the id the bridge's messages use. */
export function commandsFor(clock, opts = {}) {
  const messageId = opts.messageId || "openclaw";
  const moodId = opts.moodId || "mood";
  const heading = opts.defaultHeading || "OpenClaw";

  const statusSummary = async () => {
    const s = await clock.status();
    return {
      clock: clock.name,
      firmware: s.version,
      face: s.face,
      customFace: s.customFace || undefined,
      uptimeS: s.uptime,
      heap: s.heap,
      ip: s.ip,
      rssi: s.rssi,
      wifi: s.connected,
      safeMode: s.safeMode || 0,
      crashes: s.crashes || 0,
    };
  };

  const table = {
    // ---------------------------------------------------------------- display.*
    "display.show": {
      description: "Show a message on the clock's Message face: heading (title) and text, optional icon, colour, progress and lifetime.",
      async run(p) {
        only(p, ["heading", "text", "icon", "color", "pct", "ttl", "id", "show"], "display.show");
        const title = str(p, "heading", { max: 64 }) ?? heading;
        const text = str(p, "text", { required: true, max: 512 });
        const icon = str(p, "icon", { max: 16 });
        if (icon !== undefined && !ICONS.includes(icon)) fail(`icon must be one of ${ICONS.join(", ")}`);
        const color = str(p, "color", { max: 7 });
        if (color !== undefined && !/^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6})$/.test(color)) fail("color must be #rgb or #rrggbb");
        const pct = int(p, "pct", 0, 100);
        const ttl = int(p, "ttl", 5, 86400);
        const id = str(p, "id", { max: 15 }) ?? messageId;
        if (!/^[A-Za-z0-9_.-]+$/.test(id)) fail("id may only hold letters, digits, _ . -");
        const show = bool(p, "show") ?? true;
        const msg = { id, title: clip(title, 40), text: clip(text, 160), show };
        if (icon) msg.icon = icon;
        if (color) msg.color = color;
        if (pct !== undefined) msg.pct = pct;
        if (ttl !== undefined) msg.ttl = ttl;
        await clock.postMessage(msg);
        return { clock: clock.name, id, shown: show, clipped: msg.title !== title || msg.text !== text };
      },
    },
    "display.clear": {
      description: "Take the bridge's message and mood off the clock (or one message by id, or all messages with all: true).",
      async run(p) {
        only(p, ["id", "all"], "display.clear");
        const id = str(p, "id", { max: 15 });
        if (bool(p, "all")) { await clock.deleteMessage(""); return { clock: clock.name, cleared: "all" }; }
        const ids = id ? [id] : [messageId, moodId];
        for (const one of ids) await clock.deleteMessage(one);
        return { clock: clock.name, cleared: ids };
      },
    },
    "display.status": {
      description: "What the clock shows now: the face, and the messages waiting with their seconds left.",
      async run(p) {
        only(p, [], "display.status");
        const [s, m] = await Promise.all([clock.status(), clock.messages()]);
        return { clock: clock.name, face: s.face, customFace: s.customFace || undefined, messages: m.messages || [] };
      },
    },
    "display.brightness": {
      description: "Set the backlight, 0-100 percent (needs Integrations -> Home Assistant dashboards on the clock).",
      async run(p) {
        only(p, ["percent"], "display.brightness");
        const percent = int(p, "percent", 0, 100);
        if (percent === undefined) fail("percent is required");
        await clock.setCompat("lcd_brightness", percent);
        return { clock: clock.name, percent };
      },
    },
    // ---------------------------------------------------------------- face.set
    "face.set": {
      description: "Set Moni's mood: happy/excited (hops), thinking (waves), sad (slumps); neutral/sleepy clear it.",
      async run(p) {
        only(p, ["mood", "text"], "face.set");
        const mood = str(p, "mood", { required: true, max: 16 });
        if (!(mood in MOODS)) fail(`mood must be one of ${Object.keys(MOODS).join(", ")}`);
        const text = str(p, "text", { max: 512 });
        const m = MOODS[mood];
        if (!m) { await clock.deleteMessage(moodId); return { clock: clock.name, mood, moni: false }; }
        await clock.postMessage({ id: moodId, title: m.title, text: clip(text ?? "", 160), icon: m.icon, show: true });
        return { clock: clock.name, mood, moni: true, icon: m.icon };
      },
    },
    // ---------------------------------------------------------------- device.* (read-only)
    "device.info": {
      description: "The clock: model, firmware version and build, host name.",
      async run(p) {
        only(p, [], "device.info");
        const s = await clock.status();
        return { clock: clock.name, model: "SD PRO", platform: "esp8266", firmware: s.version, build: s.build, host: s.host };
      },
    },
    "device.status": {
      description: "Health: uptime, free heap, WiFi signal, IP, crashes, safe mode.",
      async run(p) { only(p, [], "device.status"); return statusSummary(); },
    },
    // ---------------------------------------------------------------- clock.* (screen output)
    "clock.help": {
      description: "Where the clock's instructions are: the OpenClaw skill for its firmware on GitHub, and its SKILL.md as plain text.",
      async run(p) {
        only(p, [], "clock.help");
        const s = await clock.status();
        return { clock: clock.name, ...helpLinks(s.version) };
      },
    },
    "clock.face.show": {
      description: "Switch to a face: name (a custom face) or builtin (classic, weather, big-time, agents, today, ... or its id).",
      async run(p) {
        only(p, ["name", "builtin"], "clock.face.show");
        const name = str(p, "name", { max: 20 });
        const builtin = p.builtin;
        if ((name === undefined) === (builtin === undefined)) fail("give either name (a custom face) or builtin");
        if (name !== undefined) { await clock.showFace(name); return { clock: clock.name, face: name }; }
        const id = typeof builtin === "string" ? BUILTIN_FACES[builtin] : builtin;
        if (!Number.isInteger(id) || !Object.values(BUILTIN_FACES).includes(id)) {
          fail(`builtin must be one of ${Object.keys(BUILTIN_FACES).join(", ")} (or their ids)`);
        }
        await clock.setCompat("theme", id);
        return { clock: clock.name, face: id };
      },
    },
    "clock.face.save": {
      description: "Save a custom face (the clock's face JSON, at most 4 KB, or a json-render spec, at most 6 KB) and optionally show it.",
      async run(p) {
        only(p, ["name", "face", "show"], "clock.face.save");
        const name = str(p, "name", { required: true, max: 20 });
        if (!/^[a-z0-9_-]{1,20}$/.test(name)) fail("name: 1-20 of a-z 0-9 _ -");
        if (!p.face || typeof p.face !== "object" || Array.isArray(p.face)) fail("face must be a face JSON object");
        const json = JSON.stringify(p.face);
        // A json-render spec ("root" and "elements", spec 047) is compiled by the clock: up to 6 KB.
        const max = p.face.root !== undefined && p.face.elements !== undefined ? 6144 : 4096;
        if (json.length > max) fail(`face is ${json.length} bytes minified; the clock takes at most ${max}`);
        const show = bool(p, "show") ?? false;
        await clock.saveFace(name, json, show);
        return { clock: clock.name, face: name, bytes: json.length, shown: show };
      },
    },
    "clock.catalog": {
      description: "The json-render catalog's prompt: the components the clock draws, for writing a spec to save with clock.face.save.",
      async run(p) {
        only(p, [], "clock.catalog");
        let r;
        try {
          r = await clock.catalogPrompt();
        } catch (e) {
          if (e.code === "CLOCK_ERROR" && / 404/.test(e.message)) {
            throw new InvokeError("UNAVAILABLE", `${clock.name}: its firmware has no json-render (spec 047)`);
          }
          throw e;
        }
        return { clock: clock.name, prompt: r.text ?? "" };
      },
    },
    "clock.faces.list": {
      description: "The custom faces saved on the clock, the one showing, and the variables.",
      async run(p) {
        only(p, [], "clock.faces.list");
        const f = await clock.faces();
        return { clock: clock.name, faces: f.faces || [], showing: f.showing || "", vars: f.vars || {} };
      },
    },
    "clock.vars.set": {
      description: "Set face variables (merged; null deletes one), with the clock's options ttl, history, add, persist.",
      async run(p) {
        only(p, ["vars", "ttl", "history", "add", "persist"], "clock.vars.set");
        const vars = p.vars;
        if (!vars || typeof vars !== "object" || Array.isArray(vars) || !Object.keys(vars).length) fail("vars must be an object of name: value");
        for (const [k, v] of Object.entries(vars)) {
          if (!/^[A-Za-z0-9_]{1,15}$/.test(k)) fail(`variable name ${JSON.stringify(k)}: 1-15 of A-Z a-z 0-9 _`);
          if (v !== null && !["string", "number"].includes(typeof v)) fail(`variable ${k} must be text, a number, or null (deletes it)`);
          if (typeof v === "string" && v.length > 160) fail(`variable ${k} is longer than 160 characters`);
        }
        const body = { ...vars };
        const ttl = int(p, "ttl", 1, 86400);
        if (ttl !== undefined) body.$ttl = ttl;
        for (const opt of ["history", "add", "persist"]) if (bool(p, opt)) body["$" + opt] = true;
        await clock.setVars(body);
        return { clock: clock.name, set: Object.keys(vars) };
      },
    },
  };
  return table;
}

export const COMMAND_NAMES = Object.keys(commandsFor({ name: "x" }));
