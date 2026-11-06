# 045 Research

- OpenClaw 2026.11.1 gives agents a node's command names only (spec 044 RESEARCH:
  `nodes` tool, `describe`). A name is the only hint an agent without the skill gets;
  `help` is the word it looks for.
- `raw.githubusercontent.com` serves the file as plain text, which a web-fetch tool
  reads without parsing GitHub's page. The `tree/` link is for people.
- Release tags are `v<FW_VERSION>` (`release.yml`); a version with a `-` is never
  published, so its tag would not exist: it links `main` instead.
- Putting the guide itself on the clock: `SKILL.md` is 8.9 KB, more than the 8 KB of
  room. Tool descriptions through `node.pluginTools.update`: 2-3 KB (spec 044). Both lost.
