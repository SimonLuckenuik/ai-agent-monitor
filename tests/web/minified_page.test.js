// Spec 036: the pages the clock serves are minified (scripts/minify_page.py): web/index.html
// (a file that comes with the update, spec 048) and web/fallback.html (the setup page in
// flash). Each script must parse and still define, at the top level, every function the
// markup's on* handlers call, since terser renames local names only.
//     node tests/web/minified_page.test.js
const fs = require("fs");
const os = require("os");
const path = require("path");
const { execFileSync } = require("child_process");
const root = path.join(__dirname, "..", "..");
let failures = 0;
const check = (ok, what) => { if (!ok) { failures++; console.log("FAIL " + what); } };
const lines = [];
for (const name of ["index.html", "fallback.html"]) {
  const src = path.join(root, "web", name);
  const out = path.join(os.tmpdir(), "aam-" + name.replace(".html", ".min.html"));
  execFileSync(process.platform === "win32" ? "python" : "python3", [path.join(root, "scripts", "minify_page.py"), src, "--out", out],
               { stdio: "inherit" });
  const page = fs.readFileSync(src, "utf8");
  const min = fs.readFileSync(out, "utf8");
  const script = /<script>([\s\S]*?)<\/script>/.exec(min)[1];
  try { new Function(script); } catch (e) { check(false, `${name}: the minified script does not parse: ${e.message}`); }
  const called = new Set([...page.matchAll(/\son[a-z]+="([A-Za-z_$][\w$]*)\s*[(.=]/g)].map((m) => m[1]));
  for (const fn of called) {
    const n = fn.replace(/\$/g, "\\$");
    check(new RegExp(String.raw`(function ${n}\(|(?:^|[;,{}\s])(?:const |let |var )?${n}=)`).test(script),
          `${name}: ${fn}, called from the markup, is not defined in the minified script`);
  }
  check(min.length < page.length * 0.95, `${name}: minified ${min.length} of ${page.length} bytes`);
  lines.push(`${name} ${called.size} handlers, ${page.length} -> ${min.length} bytes`);
}
console.log(`${failures ? "FAIL" : "PASS"}: minified pages: ${lines.join("; ")}`);
process.exit(failures ? 1 : 0);
