# What changed

<!-- One or two sentences. What does the device do now that it did not before? -->

# Why

<!-- The problem, not the patch. If it fixes an issue, link it. -->

# How it was tested

<!--
CI proves the firmware links and fits in flash. It cannot prove a screen draws,
a touch target is reachable or a battery reading is sane, so say which of these
you did. "Built only" is an honest and acceptable answer for a PR targeting
`dev` — firmware that has only been built, never run, is exactly what `dev` is
for. It is not acceptable for `main`.
-->

- [ ] `pio run` succeeds
- [ ] `python tools/check_docs.py`, `check_boards.py`, `check_catalog.py` and `check_frame_rules.py` are clean
- [ ] Flashed and used on hardware
- [ ] Both orientations checked (system/UI screens only — they must read `tft.width()`/`height()`, never `SCREEN_WIDTH`/`SCREEN_HEIGHT`)

**Size figures** (from your own `pio run`, if the firmware changed):

```
RAM:   x.x% (used ..... bytes from 327680 bytes)
Flash: xx.x% (used ....... bytes from 3145728 bytes)
```

# Docs in the same commit

<!--
Not a follow-up. A README claiming the wrong game count or a stale flash figure
is a defect belonging to whoever last changed the thing it describes. Tick what
applied; delete what did not.
-->

- [ ] `README.md` — feature set, game count, size figures, version
- [ ] `CHANGELOG.md`
- [ ] `CLAUDE.md` / `AGENTS.md` / the relevant directory `CLAUDE.md`
- [ ] The **About** app — and it *derives* the fact rather than restating it
- [ ] `python tools/gen_screens.py` re-run, and I looked at the PNGs
- [ ] New stills added to `PLAYABLE_STILLS`/`SYSTEM_SHOWCASE` and `SCREEN_CAPTIONS` in `tools/gen_site.py`
- [ ] Nothing here changed any of the above

# The two rules that outrank everything

- [ ] **This adds no new outbound flow.** Exactly three things leave the
      device — an NTP query, one `ip-api.com` timezone lookup, and the opt-in
      BLE beacon. If this PR changes that, stop and open an issue first: it is
      a change to what the product promises, not a feature to be reviewed on
      merit. If it changes what is transmitted or stored, the About radio page,
      the README privacy section and the installer page are part of *this*
      commit.
- [ ] **No AI attribution in the commits.** No `Co-Authored-By:` naming a
      model, no "Generated with…" footers, in commits or in this description.

<!--
Targeting: open against `dev` unless you are a maintainer cutting a release.
Please leave "Allow edits by maintainers" ticked so a check can be fixed for you.
By opening this PR you agree your contribution is GPL-3.0-or-later. No CLA.
-->
