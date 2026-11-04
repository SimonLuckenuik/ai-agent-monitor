# Progress

- 2026-11-03: proposed, after smalltv-mod's notification overlays (WTFPL).
- 2026-11-04: approved with a figure, on the Message face only. First built with the
  claude-mascot figure; the owner asked for a generic robot instead, which also becomes
  the project's logo. Built: `scripts/gen_robot.py`, the robot in five parts in the
  template, the flashes, `msg.kind`/`msg.shown`/`msg.anim`, the `msganim` setting. In
  the emulator it waves (waiting), hops then stays cheering (done), slumps (error) in
  the message's colour; warnings and errors flash; info stays still. `.bin.gz`
  +1,163 B. Integration case `message_animation`. Not yet on the clock.
- 2026-11-04: named Moni by the owner. Arms evened out (hanging and raised the same
  length), the wave only leans the forearm; a waving GIF for the README.
- 2026-11-04: released in 2.2.0. All 34 integration cases pass (the message cases again on
  the final build), unit, safety and page tests; `.bin.gz` 420,306 B. Not yet on the clock.
