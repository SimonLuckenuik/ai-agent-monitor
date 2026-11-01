// Spec 033: the settings page's POSIX rules for IANA zones, built from the runtime's own time
// zone database (Intl). Reads the code from web/index.html between its markers.
//     node tests/web/tz_rules.test.js
const fs = require("fs");
const path = require("path");
const page = fs.readFileSync(path.join(__dirname, "..", "..", "web", "index.html"), "utf8");
const start = page.indexOf("// ---- Every time zone (spec 033)");
const end = page.indexOf("// ---- end of the time zone rules");
if (start < 0 || end < 0) throw new Error("time zone code not found in web/index.html");
eval(page.slice(start, end));

let failures = 0;
function check(ok, what) { if (!ok) { failures++; console.log("FAIL " + what); } }

// Regular zones: the rule the tz database itself carries (names as the en-* locales give them).
const known = {
  "America/Toronto": "EST5EDT4,M3.2.0,M11.1.0",
  "America/Los_Angeles": "PST8PDT7,M3.2.0,M11.1.0",
  "Europe/London": "GMT0BST-1,M3.5.0/1,M10.5.0",
  "Europe/Paris": "CET-1CEST-2,M3.5.0,M10.5.0/3",
  "Australia/Sydney": "AEST-10AEDT-11,M10.1.0,M4.1.0/3",
  "Australia/Adelaide": "ACST-9:30ACDT-10:30,M10.1.0,M4.1.0/3",
  "Asia/Kathmandu": "<+0545>-5:45",
  "Asia/Tokyo": "<+09>-9",
  "UTC": "UTC0",
};
// Fixed dates so the test does not depend on the day it runs (before and after each change).
for (const from of [Date.UTC(2026, 0, 15), Date.UTC(2026, 6, 15), Date.UTC(2026, 10, 1, 12)])
  for (const [zone, rule] of Object.entries(known))
    check(posixRule(zone, from) === rule, `${zone} from ${new Date(from).toISOString()}: ${posixRule(zone, from)} (want ${rule})`);
// No fixed yearly rule: refused.
check(posixRule("Africa/Casablanca", Date.UTC(2026, 0, 15)) === null, "Casablanca refused");
// Every zone gets a rule or a refusal, never an exception; nearly all get a rule.
let none = 0;
const zones = Intl.supportedValuesOf("timeZone");
for (const z of zones) if (posixRule(z, Date.UTC(2026, 0, 15)) === null) none++;
check(none <= 10, `${none} zones without a rule`);
console.log(`${failures ? "FAIL" : "PASS"}: time zone rules, ${zones.length} zones, ${none} without a fixed rule`);
process.exit(failures ? 1 : 0);
