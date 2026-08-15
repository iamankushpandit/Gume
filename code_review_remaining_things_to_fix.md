# Code Review Remaining Things To Fix

This file tracks the review items that are still intentionally open after finishing the previously partial work on:

- `main.cpp` split into `AppRuntime.*`
- `Board` split into concern-specific implementation units
- narrowed normal-game host surface
- reproducible builds / CI / storage migration / progress batching / registry cleanup

## Still not done

1. Make each game declare its own metadata once
- Still split across `GameCatalog`, `AppRegistry`, `ScoreCatalog`, and per-game code.

2. Abstract rendering away from direct `TFT_eSPI`
- Games still render against `TFT_eSPI&` through `AppContext::display()`.
- No host-renderable canvas abstraction exists yet.

3. Avoid permanently instantiating every game if RAM becomes constrained
- `GameInstances` still owns static instances for the whole catalog.

4. Turn Launcher into the same screen/app lifecycle used by games
- System screens already use the lifecycle.
- Launcher, screensaver, and asleep are still runtime-managed views rather than ordinary apps.

5. Extend automated consistency checks to every duplicated fact
- `check_docs.py`, `check_catalog.py`, and `check_frame_rules.py` cover more than before, but not every duplicated artifact.

6. Add host-side tests
- No host-side tests yet for `Progress`, scoped storage, registry behavior, question selection, or touch rotation.

7. Add explicit storage quotas or NVS usage monitoring
- Scoped keys and schema migration exist, but there is no quota enforcement or partition usage reporting.

8. Fix child deletion semantics for persisted data more safely
- Profile name shifting remains in place.
- Broader persisted-state migration for deleted or reordered child slots is still open.

9. Validate battery divider and power detection on hardware
- `Board.h` still carries the hardware-validation TODO.

10. Revisit `beep()` if audio should be real
- The semantic API exists, but the actual `beep()` implementation is still inert and feedback is LED-only.

11. Reduce remaining `String` usage in hot or allocation-sensitive paths
- Some game hot paths were cleaned up.
- There is still residual `String` use in several subsystems, especially `Board` and some UI/system screens.

12. Test long unattended sleep/wake behavior
- The screen-sleep system exists, but long-duration wake reliability, swallowed touches, panel wake timing, and NTP interaction are not yet hardware-verified.

13. Add hardware soak tests
- No long-run test harness or documented soak procedure yet for heap stability, NVS writes, sleep/wake cycles, Wi-Fi sync, and repeated app transitions.

14. Standardize naming
- Branding still mixes `GUme`, `Gume`, and `GoodTime Kids` in different places.

15. Rendering/test SDK follow-through
- `AppContext` narrowed host capabilities, but there is still no separate input, RNG, or render abstraction suitable for host execution.
