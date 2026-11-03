// Spec 036: the page the clock embeds is web/index.html minified (scripts/minify_page.py).
// Its script must parse and still define, at the top level, every function the markup's
// on* handlers call, since terser renames local names only.
//     node tests/web/minified_page.test.js
const fs = require("fs");
const os = require("os");
const path = require("path");
const { execFileSync } = require("child_process");
const root = path.join(__dirname, "..", "..");
const out = path.join(os.tmpdir(), "aam-index.min.html");
execFileSync(process.platform === "win32" ? "python" : "python3", [path.join(root, "scripts", "minify_page.py"), "--out", out],
             { stdio: "inherit" });
const page = fs.readFileSync(path.join(root, "web", "index.html"), "utf8");
const min = fs.readFileSync(out, "utf8");
let failures = 0;
const check = (ok, what) => { if (!ok) { failures++; console.log("FAIL " + what); } };
const script = /<script>([\s\S]*?)<\/script>/.exec(min)[1];
try { new Function(script); } catch (e) { check(false, "the minified script does not parse: " + e.message); }
const called = new Set([...page.matchAll(/\son[a-z]+="([A-Za-z_$][\w$]*)\s*[(.=]/g)].map((m) => m[1]));
for (const name of called) {
  const n = name.replace(/\$/g, "\\$");
  check(new RegExp(String.raw`(function ${n}\(|(?:^|[;,{}\s])(?:const |let |var )?${n}=)`).test(script),
        `${name}, called from the markup, is not defined in the minified script`);
}
check(min.length < page.length * 0.95, `minified ${min.length} of ${page.length} bytes`);
console.log(`${failures ? "FAIL" : "PASS"}: minified page, ${called.size} handlers defined, ${page.length} -> ${min.length} bytes`);
process.exit(failures ? 1 : 0);
