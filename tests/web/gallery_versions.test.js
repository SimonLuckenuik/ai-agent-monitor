// Spec 049: the gallery offers a face only to a clock at least as new as its "firmware".
// Runs the settings page's version comparison (web/index.html) on the cases that matter.
//     node tests/web/gallery_versions.test.js
const fs = require("fs");
const path = require("path");
const page = fs.readFileSync(path.join(__dirname, "..", "..", "web", "index.html"), "utf8");
const line = (start) => {
  const i = page.indexOf(start);
  if (i < 0) throw new Error(start + " not found in web/index.html");
  return page.slice(i, page.indexOf("\n", i));
};
// const inside eval stays inside it: declared as globals instead.
eval((line("const vnum=") + "\n" + line("const newer=")).replace(/^const /gm, "var "));

let failures = 0;
function check(ok, what) { if (!ok) { failures++; console.log("FAIL " + what); } }
// newer(face's firmware, clock's version): true means the clock is too old for the face.
check(newer("2.7.0", "2.6.0"), "a 2.7.0 face on 2.6.0 is refused");
check(!newer("2.7.0", "2.7.0"), "a 2.7.0 face on 2.7.0 installs");
check(!newer("2.7.0", "2.7.0-dev"), "a development build counts as its release");
check(!newer("2.1.0", "2.10.0"), "versions compare as numbers, not text");
check(newer("2.10.0", "2.9.3"), "2.10.0 is newer than 2.9.3");
check(!newer("2.1.0", "3.0.0"), "a new major takes old faces");
check(newer("2.7.0", "0"), "an unknown version takes no face that needs one");
if (failures) { console.log(failures + " failures"); process.exit(1); }
console.log("PASS: gallery versions (7 checks)");
