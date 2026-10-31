# Plan

1. `include/firmware_validation.h`, the updater path for `.gz`; size guard in
   `scripts/build_options.py`.
2. `esp12e_nomqtt` environment for 1.4.0; `scripts/make_release.py`.
3. `.github/workflows/release.yml` on push to main; build.yml on every push.
4. README and CLAUDE.md: which file to install first.
