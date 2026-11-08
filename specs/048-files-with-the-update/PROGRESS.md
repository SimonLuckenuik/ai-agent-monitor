# 048 Progress

## 2026-11-07

- Came out of spec 047's size research (the owner: "investigate how to reduce size
  without dropping features"). Moving the settings page and the catalog to LittleFS
  was the largest lever found, and the only one that makes json-render fit with every
  feature kept. The owner: "Write spec 048".
- Written: SPEC, PLAN, RESEARCH (draft). Looked at: the update handler and
  `FirmwareValidation` (2.3.x refuses any byte past the image: the bundle needs a
  `.bin.gz` first), recovery (must not depend on storage: the fallback page), the
  read-only data in 2.3.1 by symbol size.
- Nothing built. Waiting for the owner's approval and answers to the open questions.
- The owner on the open questions: downloading from GitHub "is interesting, by default
  maybe we could show the latest release that gets downloaded and pushed to the
  device" (now requirement 0); the icons and Message face as files "good idea". Checked
  GitHub's CORS headers: the API, raw and Pages allow any origin, release downloads do
  not (RESEARCH.md), so the workflow copies the `.update` to Pages. Suggested the name
  `.update`; recommended 048 in its own release before 047 (to confirm).

## 2026-11-07 (later): approved; the starter image proposed

- The owner: "lets release 048". Status set to Approved.
- The owner asked for a proposal: a small plain `SDP...` image, released every time,
  that only gets a stock clock onto WiFi and installs the latest release from GitHub.
  Written into SPEC.md ("The starter image") and PLAN.md step 9, waiting for the go.
- The 048 work will go on its own branch cut from `faces-v2` (it releases without
  047), with these spec commits brought over.
- The owner: WiFi must come first, since the setup network has no internet; a step by
  step page. Written as "The setup steps" in SPEC.md (WiFi, switch back to the home
  network and open the new address, sign in, Install; a file by hand always there), and
  PLAN.md step 2.
- The owner: "once the clock is connected to the new network, can't it fetch itself
  whatever is required?" Checked GitHub's TLS (no smaller records: about 30 KB of heap
  for a download; two root certificates). The starter downloads by itself; the full
  firmware cannot afford TLS and keeps the browser path. SPEC, RESEARCH and PLAN
  updated.
