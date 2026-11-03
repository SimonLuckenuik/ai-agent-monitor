// Spec 032: backups that keep the custom faces. Runs the settings page's backup code from
// web/index.html (between its markers) against a mock clock.
//     node tests/web/backup.test.js
const fs = require("fs");
const path = require("path");
const page = fs.readFileSync(path.join(__dirname, "..", "..", "web", "index.html"), "utf8");
const start = page.indexOf("// ---- Backups with the custom faces (spec 032)");
const end = page.indexOf("// ---- end of the backups");
if (start < 0 || end < 0) throw new Error("backup code not found in web/index.html");
eval(page.slice(start, end));

let failures = 0;
function check(ok, what) { if (!ok) { failures++; console.log("FAIL " + what); } }

// A clock with the API's rules: faces need facesapi, at most `max` faces, customface must exist.
function clock({ facesApi = true, faces = {}, vars = {}, persist = [], max = 40 } = {}) {
  const c = { settings: { type: "smartclock-oss-settings", version: "2.0.0", facesapi: facesApi, customface: "", lang: "en" },
              faces: { ...faces }, vars: { ...vars }, persist: new Set(persist), log: [] };
  c.call = async (p, o = {}) => {
    const u = new URL(p, "http://clock"), name = u.searchParams.get("name");
    c.log.push((o.method || "GET") + " " + u.pathname + (name ? " " + name : ""));
    const ok = (text) => ({ ok: true, status: 200, text }), err = (status, text) => ({ ok: false, status, text });
    if (u.pathname === "/api/settings/export") return ok(JSON.stringify(c.settings));
    if (u.pathname === "/api/settings/import") {
      const d = JSON.parse(o.body);
      if (d.type !== "smartclock-oss-settings") return err(400, "Not a SmartClock-OSS settings file");
      let applied = 0, skipped = 0;
      for (const [k, v] of Object.entries(d)) {
        if (k === "type" || k === "version") continue;
        if (k === "customface" && v && !(v in c.faces)) { skipped++; continue; }
        c.settings[k] = v; applied++;
      }
      return ok(applied + " settings restored" + (skipped ? ", " + skipped + " skipped" : ""));
    }
    if (!c.settings.facesapi) return err(404, "Custom faces are turned off (Integrations)");
    if (u.pathname === "/api/faces" && o.method === "POST") {
      if (!(name in c.faces) && Object.keys(c.faces).length >= max) return err(400, "At most " + max + " custom faces");
      c.faces[name] = o.body; return ok("OK");
    }
    if (u.pathname === "/api/faces")
      return ok(JSON.stringify({ faces: Object.keys(c.faces), vars: c.vars, persist: [...c.persist] }));
    if (u.pathname === "/api/faces/get") return name in c.faces ? ok(c.faces[name]) : err(404, "No such face");
    if (u.pathname === "/api/faces/vars") {
      const d = JSON.parse(o.body);
      for (const [k, v] of Object.entries(d)) if (k[0] !== "$") { c.vars[k] = v; if (d.$persist) c.persist.add(k); }
      return ok("OK");
    }
    return err(404, "Not found");
  };
  return c;
}

(async () => {
  const faceA = JSON.stringify({ items: [{ type: "text", text: "A" }] }), faceB = JSON.stringify({ items: [] });
  const src = clock({ faces: { alpha: faceA, beta: faceB }, vars: { kept: "7", temp: "1" }, persist: ["kept"] });
  src.settings.customface = "beta";

  // 1. One file: settings, every face, only the persisted values.
  const b = await backupBuild(src.call);
  check(b.type === "ai-agent-monitor-backup" && b.format === 2 && b.version === "2.0.0" && b.date, "backup header");
  check(JSON.stringify(Object.keys(b.faces)) === '["alpha","beta"]', "both faces in the backup");
  check(JSON.stringify(b.faces.alpha) === faceA, "face JSON kept as is");
  check(JSON.stringify(b.vars) === '{"kept":"7"}', "only persisted values: " + JSON.stringify(b.vars));
  check(!JSON.stringify(b).includes("password"), "no secrets");

  // 2. Restore onto a blank clock with faces off: settings turn them on, then faces, then the face shown.
  const dst = clock({ facesApi: false });
  const lines = await backupRestore(dst.call, JSON.stringify(b));
  check(Object.keys(dst.faces).length === 2 && dst.faces.alpha === faceA, "faces restored");
  check(dst.settings.customface === "beta", "shown face restored after the faces exist");
  check(dst.vars.kept === "7" && dst.persist.has("kept") && !("temp" in dst.vars), "persisted value restored");
  check(lines[0] === "3 settings restored", "settings report: " + lines[0]);
  check(lines.includes("2 custom faces restored") && lines.includes("1 saved value restored"), "report: " + lines);
  const posts = dst.log.filter((x) => x.startsWith("POST"));
  check(posts[0] === "POST /api/settings/import" && posts[posts.length - 1] === "POST /api/settings/import", "settings first and last");

  // 3. Faces refused for room are named, the rest restored.
  const full = clock({ max: 1 });
  const l3 = await backupRestore(full.call, JSON.stringify(b));
  check(l3.includes("1 custom face restored") && l3.some((x) => x === "Not restored: beta: At most 1 custom faces"), "refused face: " + l3);

  // 4. A backup made with faces off holds settings only, and restores.
  const off = clock({ facesApi: false, faces: { alpha: faceA } });
  const b4 = await backupBuild(off.call);
  check(b4.faces === null, "faces off: settings only");
  const l4 = await backupRestore(clock().call, JSON.stringify(b4));
  check(l4.length === 1 && /settings restored/.test(l4[0]), "settings-only backup: " + l4);

  // 5. Faces off on the clock, and the backup does not turn them on: reported once.
  const b5 = JSON.parse(JSON.stringify(b)); b5.settings.facesapi = false;
  const l5 = await backupRestore(clock({ facesApi: false }).call, JSON.stringify(b5));
  check(l5.some((x) => /^Not restored: 2 faces: Custom faces are turned off/.test(x)) && !l5.some((x) => /saved value/.test(x)), "faces off: " + l5);

  // 6. An old settings-only file restores as before; anything else is refused.
  const old = clock();
  const l6 = await backupRestore(old.call, JSON.stringify({ type: "smartclock-oss-settings", lang: "fr" }));
  check(old.settings.lang === "fr" && l6.length === 1, "old backup: " + l6);
  for (const bad of ["not json", "{}", JSON.stringify({ type: "ai-agent-monitor-backup", format: 3, settings: {} })]) {
    let threw = false;
    try { await backupRestore(clock().call, bad); } catch (e) { threw = true; }
    check(threw, "refused: " + bad);
  }

  console.log(failures ? failures + " failed" : "backup: all passed");
  process.exit(failures ? 1 : 0);
})();
