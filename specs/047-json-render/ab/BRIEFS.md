# A/B briefs (spec 047)

The same three briefs for both arms. The clock is a 240x240 colour display on a desk,
seen from about a metre away, with no input. Make each screen look as good as you can:
it should be clear at a glance, and pleasant to keep on a desk.

1. **ci**: the CI status of a repository. Variables pushed by an agent: `repo` (text),
   `branch` (text), `build` (`ok`, `running` or `fail`), `pct` (0-100, progress while
   running), `passed` and `failed` (test counts). Show the time somewhere small.
2. **weather**: a weather clock. The time and date large and readable, the current
   weather (icon, temperature, conditions), and the next three days (day name, icon,
   high/low). Use the clock's own weather values, no variables.
3. **meeting**: a meeting countdown. Variables: `meeting` (title), `room` (text),
   `mins` (minutes until it starts, a number). Make it urgent-looking when `mins` is 5
   or less.

Values used for the screenshots: repo `ai-agent-monitor`, branch `faces-v2`, build
`fail` (and `running` with pct 64), passed 402, failed 3; meeting `Design review`, room
`Lab 2`, mins 12 (and 4); time 10:42 on 5 October 2026, Montreal weather (18 C, partly
cloudy).
