# Plan

1. `scripts/gen_robot.py`: the robot, its parts, and the template's sprite items; the
   logo and a sheet of its poses in `docs/images/`.
2. `src/faces.cpp`: the Message face's template gets the robot, the icon's `show`,
   `maps` and `invert`; `messageValueAt` answers `kind`, `shown` (from the time the face
   last drew in full) and `anim`.
3. `src/config.h`, `src/main.cpp`, `web/index.html`: the `msganim` setting (default on),
   the *Animated messages* switch on the Display card.
4. Checks: the integration case `message_animation` (the robot moves for waiting and
   done, done settles after 6 s, an error flashes and stops after 4 s, nothing moves
   with the setting off); `message_layouts` screens; memory beside the robot; the flash
   size.
5. Docs: integrations API (Agent messages, `msg.` fields), settings, README logo,
   CHANGELOG.
6. Release with the next firmware (2.2.0); then look at it on the clock.
