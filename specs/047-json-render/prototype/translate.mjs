// Spec 047 prototype: a json-render spec built from the clock catalog -> a clock face.
// Not the feature: no dependencies, rough text widths, a subset of the catalog.
//   node translate.mjs sample-build.json > face.json
import { readFileSync } from "node:fs";

const SPEC = JSON.parse(readFileSync(process.argv[2], "utf8"));
const W = 240, H = 240;

// --- Values: json-render dynamic props -> clock placeholders and expressions ----------
// /time/hm, /now/temp, /day0/high ... are the clock's own (dotted); /build is a variable.
const CLOCK = new Set(["time", "date", "now", "day0", "day1", "day2", "day3", "day4", "sun",
  "moon", "units", "loc", "theme", "msg", "net", "sys", "age"]);
function ph(path) {
  const p = path.replace(/^\//, "").split("/");
  if (CLOCK.has(p[0])) return p.join(".");
  if (p.length !== 1) throw new Error(`state path ${path}: variables are one level (/name)`);
  return p[0];
}
function cond(c) {                       // a visibility or $cond condition -> expression
  if (Array.isArray(c)) return c.map(cond).join("&&");
  if (c.$and) return c.$and.map(cond).join("&&");
  if (c.$or) return c.$or.map(cond).join("||");
  const v = `{${ph(c.$state)}}`;
  if ("eq" in c) return `${v}${c.not ? "!=" : "=="}${c.eq}`;
  if ("neq" in c) return `${v}!=${c.neq}`;
  if ("gt" in c) return `${v}>${c.gt}`;
  if ("lt" in c) return `${v}<${c.lt}`;
  return c.not ? `!${v}` : v;
}
function val(x, inText = false) {
  if (x === null || typeof x !== "object") return x;
  if (x.$state) return `{${ph(x.$state)}${x.fallback !== undefined ? "|" + x.fallback : ""}}`;
  if (x.$template) return x.$template.replace(/\$\{([^}|]+)(\|[^}]*)?\}/g, (_, p, f) => `{${ph(p)}${f || ""}}`);
  if (x.$cond) {
    const e = `${cond(x.$cond)}?${val(x.$then)}:${val(x.$else)}`;
    return inText ? `{${e}}` : e;
  }
  throw new Error(`unsupported dynamic value ${JSON.stringify(x)}`);
}

// --- Sizes: the clock's fonts ----------------------------------------------------------
const SIZES = { sm: { font: 2, h: 16, cw: 7 }, md: { font: 4, h: 26, cw: 14 },
  lg: { font: "big", size: 36, h: 36, cw: 22 }, xl: { font: "big", size: 60, h: 60, cw: 36 } };
const textLen = (p) => p.maxChars ?? String(typeof p.text === "string" ? p.text : "--------").replace(/\{[^}]*\}/g, "8888").length;

// --- Layout: measure, then place (a small flex: Column, Row, Card, Spacer) --------------
const el = (id) => { const e = SPEC.elements[id]; if (!e) throw new Error(`no element ${id}`); return e; };
const pad = (p) => p.padding ?? 0;
function measure(id) {                   // intrinsic [w, h]
  const e = el(id), p = e.props ?? {}, kids = e.children ?? [];
  switch (e.type) {
    case "Text": case "Clock": { const s = SIZES[p.size ?? "sm"]; return [textLen(p) * s.cw, s.h]; }
    case "Icon": case "Dot": return [p.size ?? 24, p.size ?? 24];
    case "Progress": return [p.width ?? 100, p.height ?? 8];
    case "Ring": return [2 * (p.radius ?? 30), 2 * (p.radius ?? 30)];
    case "Sparkline": return [p.width ?? 100, p.height ?? 40];
    case "Spacer": return [p.size ?? 0, p.size ?? 0];
    case "Row": case "Column": case "Card": case "Screen": {
      const m = kids.map(measure), g = p.gap ?? 0, row = e.type === "Row";
      const main = m.reduce((a, s) => a + (row ? s[0] : s[1]), 0) + g * Math.max(0, kids.length - 1);
      const cross = Math.max(0, ...m.map((s) => (row ? s[1] : s[0])));
      const w = p.width ?? (row ? main : cross) + 2 * pad(p), h = p.height ?? (row ? cross : main) + 2 * pad(p);
      return [w, h];
    }
    default: throw new Error(`${id}: "${e.type}" is not in the clock catalog`);
  }
}

const items = [];
function place(id, x, y, w, h, bg) {
  const e = el(id), p = e.props ?? {}, show = e.visible ? cond(e.visible) : undefined;
  const add = (it) => items.push(show ? { ...it, show } : it);
  const color = p.color !== undefined ? val(p.color) : undefined;
  switch (e.type) {
    case "Text": case "Clock": {
      const s = SIZES[p.size ?? "sm"], a = p.align ?? "left";
      const text = e.type === "Clock" ? `{time.${p.format ?? "hm"}}` : val(p.text, true);
      const ax = a === "center" ? x + w / 2 : a === "right" ? x + w : x;
      const it = { type: "text", x: Math.round(ax), y: Math.round(y + h / 2), text, font: s.font,
        align: { left: "ml", center: "mc", right: "mr" }[a] };
      if (s.size) it.size = s.size;
      if (color) it.color = color;
      if (bg && p.live !== false) { it.bg = bg; it.pad = Math.max(0, Math.round((w - textLen(p) * s.cw) / 2)); }
      return add(it);
    }
    case "Icon": {
      const it = { type: "icon", x: Math.round(x + w / 2), y: Math.round(y + h / 2), size: p.size ?? 24, icon: val(p.name) };
      if (color) it.color = color; if (bg) it.bg = bg; return add(it);
    }
    case "Dot": return add({ type: "circle", x: Math.round(x + w / 2), y: Math.round(y + h / 2), r: Math.round((p.size ?? 24) / 2), ...(color && { color }), ...(bg && { under: bg }) });
    case "Progress": return add({ type: "bar", x, y: Math.round(y + (h - (p.height ?? 8)) / 2), w, h: p.height ?? 8, value: val(p.value), max: p.max ?? 100, ...(color && { color }), ...(p.track && { track: val(p.track) }) });
    case "Ring": { const r = p.radius ?? 30; return add({ type: "ring", x: Math.round(x + w / 2), y: Math.round(y + h / 2), r, w: p.thickness ?? 6, value: val(p.value), max: p.max ?? 100, ...(color && { color }), ...(p.track && { track: val(p.track) }) }); }
    case "Sparkline": return add({ type: "spark", x, y, w, h, var: ph(p.state), ...(color && { color }), ...(p.fill && { fill: val(p.fill) }), ...(bg && { bg }) });
    case "Spacer": return;
    case "Row": case "Column": case "Card": case "Screen": {
      if (e.type === "Card" || p.background) {
        const r = { type: "rect", x, y, w, h, r: p.radius ?? (e.type === "Card" ? 10 : 0) };
        if (p.background) { r.color = val(p.background); bg = r.color; }
        if (p.border) { r.border = val(p.border); r.bw = p.borderWidth ?? 2; }
        if (e.type !== "Screen") add(r);
      }
      const kids = e.children ?? [], row = e.type === "Row", g = p.gap ?? 0, P = pad(p);
      const iw = w - 2 * P, ih = h - 2 * P, m = kids.map(measure);
      const sizes = m.map((s) => (row ? s[0] : s[1]));
      const grow = kids.map((k) => el(k).props?.grow ?? 0), tg = grow.reduce((a, b) => a + b, 0);
      const free = (row ? iw : ih) - sizes.reduce((a, b) => a + b, 0) - g * Math.max(0, kids.length - 1);
      if (tg > 0) kids.forEach((_, i) => (sizes[i] += Math.floor((free * grow[i]) / tg)));
      const j = p.justify ?? "start", n = kids.length, left = tg > 0 ? 0 : Math.max(0, free);
      let pos = (row ? x : y) + P + (j === "center" ? left / 2 : j === "end" ? left : 0);
      const gap = j === "between" && n > 1 ? g + left / (n - 1) : g;
      kids.forEach((k, i) => {
        const al = p.align ?? "stretch", cs = row ? m[i][1] : m[i][0], cmax = row ? ih : iw;
        const c = al === "stretch" ? cmax : cs;
        const off = al === "center" ? (cmax - c) / 2 : al === "end" ? cmax - c : 0;
        const cx = row ? pos : x + P + off, cy = row ? y + P + off : pos;
        place(k, Math.round(cx), Math.round(cy), Math.round(row ? sizes[i] : c), Math.round(row ? c : sizes[i]), bg);
        pos += sizes[i] + gap;
      });
      return;
    }
  }
}

const root = el(SPEC.root);
if (root.type !== "Screen") throw new Error("the root must be a Screen");
const face = {};
if (root.props?.background) face.bg = val(root.props.background);
if (SPEC.state?.$maps) face.maps = SPEC.state.$maps;     // see RESEARCH.md: maps have no json-render form
place(SPEC.root, 0, 0, W, H, face.bg);
face.items = items;
if (items.length > 32) throw new Error(`${items.length} items: the clock takes 32`);
const out = JSON.stringify(face);
if (out.length > 4096) throw new Error(`${out.length} bytes: the clock takes 4096`);
process.stdout.write(JSON.stringify(face, null, 1) + "\n");
