// Prints the POSIX rule the settings page gives each time zone (spec 033's posixRule, read
// from web/index.html), one "zone rule" per line, for tz_rule_tests.cpp (spec 036).
//     node tests/tz/zone_rules.js > rules.txt
const fs = require("fs");
const path = require("path");
const page = fs.readFileSync(path.join(__dirname, "..", "..", "web", "index.html"), "utf8");
const start = page.indexOf("// ---- Every time zone (spec 033)");
const end = page.indexOf("// ---- end of the time zone rules");
if (start < 0 || end < 0) throw new Error("time zone code not found in web/index.html");
eval(page.slice(start, end));
for (const zone of Intl.supportedValuesOf("timeZone")) {
  const rule = posixRule(zone, Date.UTC(2026, 0, 15));
  if (rule) console.log(zone + " " + rule);
}
