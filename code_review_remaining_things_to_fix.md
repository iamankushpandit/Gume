# Code Review Remaining Things To Fix

This file tracks the review items that are still intentionally open after finishing the previously partial work on:

- `main.cpp` split into `AppRuntime.*`
- `Board` split into concern-specific implementation units
- narrowed normal-game host surface
- reproducible builds / CI / storage migration / progress batching / registry cleanup
- per-game launcher metadata ownership
- Launcher and Profiles running through normal screen lifecycle
- system-app capability declarations and runtime guards
- app-facing renderer abstraction (`Ui::Renderer`) with a `TftRenderer` firmware adapter

## Still not done

1. Avoid permanently instantiating every game if RAM becomes constrained
- `GameInstances` still owns static instances for the whole catalog.

2. Extend automated consistency checks to every duplicated fact
- `check_docs.py`, `check_catalog.py`, and `check_frame_rules.py` now cover metadata order/icon/default visibility, `AppGame` subclassing, system capabilities, docs/site/screens and frame-rule ratchets, but not every duplicated artifact.

3. Add host-side tests
- No host-side tests yet for `Progress`, scoped storage, registry behavior, question selection, or touch rotation.
- Rendering now has an interface, but there is no fake/recording renderer test backend yet.

4. Add explicit storage quotas or NVS usage monitoring
- Scoped keys and schema migration exist, but there is no quota enforcement or partition usage reporting.

5. Fix child deletion semantics for persisted data more safely
- Profile name shifting remains in place.
- Broader persisted-state migration for deleted or reordered child slots is still open.

6. Validate battery divider and power detection on hardware
- `Board.h` still carries the hardware-validation TODO.

7. Revisit `beep()` if audio should be real
- The semantic API exists, but the actual `beep()` implementation is still inert and feedback is LED-only.

8. Reduce remaining `String` usage in hot or allocation-sensitive paths
- Launcher, Settings, Profiles, Counting and Scores were reduced and the frame-rule ratchet was lowered.
- There is still residual `String` use in several subsystems, especially `Board` and some UI/system screens.

9. Test long unattended sleep/wake behavior
- The screen-sleep system now logs and exposes sleep/wake counters and panel wake delay in System Info, but long-duration wake reliability, swallowed touches and NTP interaction are not yet hardware-verified.

10. Add hardware soak tests
- No long-run test harness or documented soak procedure yet for heap stability, NVS writes, sleep/wake cycles, Wi-Fi sync, and repeated app transitions.

11. Standardize naming
- Branding still mixes `GUme`, `Gume`, and `GoodTime Kids` in different places.

12. Host execution SDK follow-through
- `AppContext` narrowed host capabilities and rendering now goes through `Ui::Renderer`, but there is still no separate input or RNG abstraction and no host runner.
