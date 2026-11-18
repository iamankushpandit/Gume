# src/hal

The only place that talks to hardware. Ordinary games should not reach this directly; system screens use `GameHost::board()` and should prefer the narrow access facets when they only need one concern.

## Which board, and how this code knows

**Nothing in here may name a GPIO number, a panel size or a divider ratio.** Every hardware fact comes from `BOARD`, the one `BoardProfile` constant selected at compile time by `GUME_BOARD_HEADER` and defined in `include/boards/<id>.h`. That is what lets a second board be a header rather than a patch, and it is the rule that stops this directory becoming board-specific again.

A peripheral the board does not wire is `PIN_NONE`, and the caller guards: `BOARD.hasSdSlot()`, `hasRgbLed()`, `hasSpeaker()`, `hasBatterySense()`, `hasBacklightControl()`. Degrade quietly rather than fail -- a board with no battery sense (`adcPin = PIN_NONE`, the dual-USB CYD) shows no battery badge anywhere, raises no low-battery warning and starts no sampling task -- `Ui::batteryBadgeWidth()` returns 0 and `drawBatteryBadge()` draws nothing, so every header reclaims the space -- and needs no code change here at all. Adding a field to the contract means filling it in for every existing board in the same commit; `tools/check_boards.py` is what notices when that did not happen. `docs/PORTING.md` has the rest.

## Board.* + BoardAccess.h + BoardStorage.cpp + BoardStorageMaintenance.cpp

`Board.h` remains the legacy aggregate for existing call sites, but it also exposes no-vtable concern facades: `displayAccess()`, `touchAccess()`, `storageAccess()`, `powerAccess()`, `networkAccess()` and `feedbackAccess()`. New subsystem code should take the narrowest facade it can. The implementation is split by concern:

- `Board.cpp` - bring-up, profiles, layout/idle settings, visibility cache
- `BoardAccess.h` - narrow display/touch/storage/power/network/feedback forwarding facades
- `BoardDisplay.cpp` - TFT access, rotation, BMP blitting
- `BoardTouch.cpp` - touch ADC, calibration, coordinate mapping
- `BoardPower.cpp` - battery telemetry, backlight brightness, panel sleep/wake
- `BoardNetwork.cpp` - Wi-Fi credentials, timezone, NTP sync, network activity log
- `BoardUpdate.cpp` - the update-availability check: fetch, parse, version
  compare, and the once-a-day gate. A notice, never an update -- nothing here
  downloads or installs firmware. See `include/UpdateChannel.h`. It compares
  `BRAINO_VERSION`, the one place under `src/` that reads it: everything that
  shows a version shows `FIRMWARE_VERSION`, which adds `+<device>`. An
  experimental image (`GUME_EXPERIMENTAL`) takes only its own board's line of
  the manifest, never the `*` fallback
- `BoardFeedback.cpp` - RGB LED, BLE and Nearby enable switches
- `AudioSynth.cpp` - the per-sample synthesiser: script, oscillator,
  resonators, gain. Pure C++, rendered off the device by
  `test/host/audio_synth_test.cpp`
- `BoardAudio.cpp` - arming the synthesiser, and the `braino-audio` task
  that runs it
- `BoardAudioCues.cpp` - the cue tables and the phoneme table for the
  spoken boot phrase
- `Sound.h` - the cue vocabulary, shared with app code without pulling in
  `Board.h`
- `BoardStorage.cpp` - scoped persistence, schema versioning, migration
- `BoardStorageMaintenance.cpp` - profile-slot moves, NVS usage telemetry
- `TouchTypes.h` - `TouchPoint` shared with app code without pulling in `Board.h` or the TFT driver

### Persistence and profiles

NVS via `Preferences`, in the legacy application namespace.

`BoardStorage.cpp` owns profile-scoped persistence, the storage schema version and the legacy-key migrator. `BoardStorageMaintenance.cpp` owns NVS usage telemetry and profile-slot moves. `scopedKey()` is **private**. It prefixes `p{N}_` and translates plain game keys into compact app-scoped leaves inside `getScore` / `setScore` / `saveBestScore` / `worstScore` / `loadBlob` / `saveBlob`, so callers pass a plain key and get per-profile storage for free. Never construct a prefixed key by hand.

Five player slots plus a permanent Guest (`MAX_PLAYERS = 5`, `GUEST_INDEX = 5`, names capped at `PROFILE_NAME_MAX = 9` — `NAME_MAX` is a POSIX macro, hence the odd name). **Guest drops every write** through the scoped setters; that is deliberate, not a bug to fix.

Per-profile: scores, best/worst, mastery blobs, game visibility (`gameVisibleFor`). Visibility is one `p<N>_gvis` blob keyed by each game's id, never its launcher index -- `GameVisibility.h` is the table, pure and host-tested, and `Board` keeps the active player's in RAM because the launcher asks it for every tile. Schema 3 (`migrateGameVisibility()`, called by the runtime after `begin()` because only it knows which game is at which index) folds the old per-index `p<N>_gv<I>` bools in. Global: theme, layout, screen flip, brightness, Wi-Fi credentials, NTP, NTP resync interval, timezone.

**One slot is the console's own.** `loadConsoleBlob()` / `saveConsoleBlob()` /
`clearConsoleBlob()` keep a blob under `CONSOLE_SLOT` (9), outside the players'
slots: the state of a game being played against another console. It is not a
player's data -- the other console is playing this console, whoever is holding
it -- so a change of player does not touch it, removing a player does not move
or clear it, and Guest's rule that nothing is written does not apply to it.
Scores and progress never go there, and nothing in it is transmitted.

Deleting a player is a storage operation, not a name-list edit. `removePlayer()` clears the deleted slot's `pN_` keys, moves every later slot down by enumerating NVS entries in the application namespace, copies supported NVS types with fixed static scratch buffers, and clears the old last slot. Deleting the active player switches to Guest; deleting a lower slot shifts the active index down so the same real player stays active.

`storageTelemetry()` reports default-partition NVS entries plus the application and watchdog namespace entry counts. System Info -> Memory shows that data with 80% warning and 92% critical soft quota thresholds. Do not call it from per-frame render paths; it enumerates flash-backed metadata.

`factoryReset()` wipes everything in the application namespace and reboots. The watchdog crash namespace is deliberately separate.

### Touch

**Two kinds of controller, one coordinate.** `BOARD.touch.kind` says which the board wires, and `pollTouch()`'s rotation switch is shared by both — game code never adjusts coordinates itself either way.

*Resistive (XPT2046).* Bit-banged SPI (the TFT owns HSPI). `readResistiveTouch()` averages samples and applies `BOARD.touch.pressureThreshold`; `mapTouch()` runs the 3-point affine fit, which persists in NVS behind a magic number. `runTouchCalibration()` re-runs the wizard.

*Capacitive (FT6336U).* I²C. `readCapacitiveTouch()` does one 5-byte read from `TD_STATUS`, which is immediately followed by the first touch point — count and coordinates in a single transaction per frame. Two things about it are load-bearing:

- **It reports the panel's NATIVE portrait frame and has never heard of `setRotation()`.** `mapTouch()` rotates into the same landscape frame the affine fit produces, so everything downstream sees one kind of coordinate. **That quarter-turn is a property of the panel, not a convention** — it depends on which physical corner the controller calls its origin, and it was established on hardware, not derived: `src/s3_diag.cpp` drew a crosshair through this exact transform, tracked a finger at all four rotations, and carried the opposite handedness on a key to show it was wrong.
- **`hasTouchCalibration()` returns true unconditionally, and `runTouchCalibration()` returns immediately.** There is nothing to fit. This is not a convenience: every caller that sees `false` offers or forces the wizard, and on a capacitive panel that wizard **cannot be completed** — the first crosshair is a screen the owner never gets past. `pressure` is synthesised as `0xFFFF` because capacitive contact is binary and every caller only ever compares it to a threshold; reporting 0 would make a real press look like noise.

**Use `#if GUME_TOUCH_CAPACITIVE`, not `if constexpr`, to separate the two.** In a non-template function both arms of an `if constexpr` are still compiled, so the capacitive arm alone linked Wire into every resistive build — measured at **+4,476 bytes** of flash for code that can never run, on a budget already at 75%. The macro lives in the board header and the profile's `TouchKind` is *derived from it*, so there is still exactly one statement of the fact and the two cannot drift.

`TouchPoint` lives in `TouchTypes.h` and carries `down`, `justPressed`, `justReleased`, `x`, `y`, `pressure`; edge flags come from diffing against `lastTouch_`.

### Display

Owns the `TFT_eSPI` instance. Calibration is captured in rotation 1 and derived for other rotations by transform, so `setDisplayRotation()` is safe at runtime. All four rotations are reachable: the profile supplies landscape and portrait, and `screenFlipped()` -- an owner's setting, for an enclosure that holds the board the other way up -- adds half a turn to either. That sum is done by the runtime, in `BrainoApp::effectiveRotation()`; nothing in this directory picks a rotation except the calibration wizard, which borrows rotation 1 and puts back what it found. Brightness is PWM on the backlight pin with a floor of `BRIGHTNESS_MIN = 25` — below that the panel is unreadable and a player could not find the slider to undo it.

**Waking a panel is Sleep Out AND Display ON.** `displayWake()` sends `0x11`
and then `0x29`, unconditionally. Sleep Out alone is enough on the ILI9341 --
the panel this code was written against -- and is NOT enough on the ST7796 on
the 4-inch board, which resumes with display output still disabled. The failure
is the nastiest shape available: the firmware wakes perfectly, touch is read,
the wake path runs and the log prints a normal 120ms panel delay, and the glass
stays black. An owner sees a dead device and reaches for the reset button.
Measured on an E32R40T, where two sleep/wake cycles both logged
`[display] wake ... panel delay 120ms` with nothing visible.

Display ON is a no-op on a panel that is already displaying, so it is sent for
every board rather than behind a `BOARD` test. A per-panel branch here would be
one more thing for the next controller to get wrong, and this is exactly the
class of difference that produces a healthy log and a dark screen. Do not
"optimise" it back into a conditional.

Panel sleep/wake is observable: `displaySleepTelemetry()` reports sleep count, wake count, last sleep/wake times, last sleep duration and panel wake delay. `BoardPower.cpp` also logs `[display] sleep ...` and `[display] wake ...` lines over serial, and System Info shows the same counters under App -> Display sleep. This is instrumentation for soak testing, not a substitute for a physical long-duration run.

### Sound

`Sound.h` is the vocabulary; `BoardAudio.cpp` is everything else. Read the
header comment in that file before changing it -- three of its decisions look
arbitrary and are not.

**Nothing here is a recording and nothing here may become one.** Every sound is
generated a sample at a time from a script of `Segment`s: struck notes that
ring and glide, sine or square with an optional linear sweep, band-passed
noise, and three formant resonators
driven either by a monotone impulse train (voiced) or by noise (unvoiced).
That is a flash decision -- a second of 16-bit 16 kHz mono is 32 KB against a
budget already at 76.0% -- and it is also what makes a spoken phrase possible
at all. The whole vocabulary plus the phrase is under a kilobyte of const data.

**`playSound()` arms a script; it does not play it.** A dedicated FreeRTOS
task, `braino-audio`, generates the samples: priority 3 against the loop task's
1, pinned to the same core so that it *preempts* a long frame.

**It is a task and not a tick, and that is the second version.** The first
generated from `tickAudio()` in the loop, arguing that the DMA is far deeper
than a frame -- 6 x 256 frames is 96 ms on the codec at 16 kHz and 64 ms on the
built-in DAC at 24 kHz, against a 20 ms budget. True of a typical frame; false
of the frame that matters. A full-screen repaint is ~150 KB over SPI, and a
launcher page turn is that plus every tile and icon, comfortably past 96 ms on
the 4-inch panel. So a cue armed just before one would fill the DMA, and the
DMA would run dry in the middle of the repaint. That is what the launcher's
Previous and Next sounded like, and it was reported twice before the cause was
found. Deepening the buffer would have moved the threshold, not removed it --
the worst frame is bounded by nothing. A task is.

The synthesiser state is shared with the loop, so `audioLock` guards arming and
generation. **It is not held across the blocking write**: the task generates a
128-sample block into its own buffer under the lock, releases it, and only then
blocks inside `i2s_write()`. So `playSound()` from game code waits for at most
one block, never for the DMA. Never invert that.

`tickAudio()` still exists and the runtime still calls it, but only as the
fallback for a device where the task could not be created at all. It shares
`generateBlock()` with the task so the one genuinely error-prone part -- the
built-in DAC's offset-binary word format -- has a single definition. Samples
the DMA would not take on that path are held in `pending` and written first
next frame rather than dropped: the synthesiser cannot be run backwards, and
dropping them is audible as a stutter on anything long enough to fill the DMA.

`src/s3_diag.cpp` blocks the whole way through a note, and is right to: a
bring-up probe has no frame budget.

**Most cues are Notes: struck, left to ring, often gliding.** `Wave::Note`
(`AudioSynth.cpp`) is the smooth set the owner chose on 2026-11-15. A Note
takes one of `NOTE_VOICES` (4) voices of its own, rises over its timbre's
attack, falls exponentially towards its sustain (zero for all but the piano's
KEYS), glides linearly from `a` to `b` across its segment -- or across the
timbre's own `glideMs` -- and rings on under whatever segment follows. A
segment's `ms` is when the NEXT thing starts, not when the note stops. The
timbres are a table in `AudioSynth.cpp`, named by `audio::Timbre`; all are
pure sines except BELL's soft octave. Five things are load-bearing:

- **A script with no Note in it generates bit for bit what it did before.**
  Notes are summed after the shared gain and only while one sounds, so
  GameOver and the spoken phrase were not touched.
- **A taken voice is faded, not cut** (`declick`), and the sum of voices
  goes through a soft ceiling from 75% of full scale rather than the hard
  clip, which would square the peaks a chord makes.
- **A script of Notes is never a continuation of itself.** The same note
  asked for again is struck again -- a key pressed twice. A key still down
  is `Sound::NoteHold` (`synthHold()`), which gives the note its segment
  again without striking it; PianoGame sends it every
  `SOUND_NOTE_REARM_MS`, and the static_assert in `BoardAudio.cpp` still
  states the margin. Re-arming any OTHER script while it sounds is still a
  continuation that keeps the phase and the resonators.
- **A Note ends at a floor**: -66 dB of full scale on the codec and the I2S
  amplifier, just under one 8-bit step at full volume on the built-in DAC
  (`synthSetNoteFloor()`), where a note left to fall further only buzzes.
- **Mute fades the notes in 4 ms**, as `synthFadeOut()` fades the rest.

`host/sound/` renders every cue to WAV through these same files (see
`host/CLAUDE.md`), and `test/host/audio_synth_test.cpp` fails on a step any
Note shape makes.

**The voice is a phoneme table, not text-to-speech.** `PHRASE_LETS_PLAY_BRAINO`
spells "Let's play Braino!" out as L EH T S / P L EY / B R EY N OW, each entry
being the first three formant frequencies of a vocal tract shaped to say it.
There is no dictionary and no grapheme rules, so a second phrase means writing
its phonemes by hand -- which is the intended cost, not an oversight. Three
things in that table are load-bearing and are commented as such: a diphthong is
two segments, a stop is a silence *and then* a burst, and the fricative has to
be longer than a vowel to be audible at all.

**Mute and volume are global device settings, and both are RAM-mirrored.**
`soundEnabled()` sits on the path of every cue in every game, so reading it
from flash-backed Preferences per call is exactly the hot-path NVS read the
responsiveness rule is about. The mute gate lives in `Board::playSound()` and
nowhere else, which is what makes it true of the beeps and the boot phrase as
well as of the cues; `setSoundEnabled(false)` also stops whatever is currently
sounding, because Mute is the control somebody reaches for *while* the boot
phrase is playing. `setVolume()` clamps to `AUDIO_VOLUME_MAX` (now read from
`BOARD.audio.maxVolume`).

**The audio backend is one of three, or none.**

- `GUME_HAS_AUDIO_CODEC 1` — ES8311 codec (Freenove FNK0104B). Requires
  `Wire` + `driver/i2s.h`. Volume via register 0x32 (logarithmic — see
  `applyCodecVolume()`). `maxVolume` = 85 on this board.
- `GUME_HAS_AUDIO_DAC 1` — ESP32 built-in DAC via `I2S_DAC_BUILT_IN` (CYD
  boards: E32R28T-1, the inverted-panel ESP32-2432S028, E32R32P, E32R40T). Requires `driver/i2s.h`
  only; no Wire. Volume is linear amplitude scaling applied at sample time in
  `tickAudio()` — do **not** copy the dB conversion from `applyCodecVolume()`,
  which is correct only for a logarithmic register. Samples must be converted
  from signed int16 to unsigned offset-binary: `(sample * vol/100) + 32768`.
  Only the high 8 bits reach the DAC, giving 8-bit resolution. `maxVolume` =
  75 on these boards (the DAC feeds an onboard amplifier; the 1-inch driver
  distorts above 75%).
- `GUME_HAS_AUDIO_I2S 1` — a bare I2S amplifier with no codec (Attaky
  Core: the Media I/O module's NS4168). The codec path with the codec taken
  out: no Wire, no register writes, no MCLK (`i2sMclk = PIN_NONE` routes
  none), the same 16-bit `STAND_I2S` frame on both slots that is known good on
  the FNK0104B, so whichever slot the amplifier's CTRL strap picks carries the
  sound. Volume is the DAC's linear multiplier, applied to the signed sample in
  `generateBlock()` -- not the codec's dB maths. `tx_desc_auto_clear` is on,
  because signed zero is silence here. `AUDIO_RATE` is 16000, **chosen rather
  than measured**: the S3's I2S clock is 160 MHz through a fractional divider
  and 16000 x 256 is 39 1/16 of it, exact on paper, but the drain timing the
  DAC's table came from has not been done on this board. `maxVolume` = 50,
  provisional until set by ear.
- None defined — no audio. No supported board is in that state now, but the
  Attaky Core was until it had the I2S backend, and it was the first board
  to compile this arm at all: three `using namespace audio;` directives sat
  outside the guard that defines the namespace, and every build failed. They
  are guarded now. If you touch these files, build with the board header's
  audio macro removed as well.

Every guard here tests `GUME_HAS_AUDIO` -- defined in `BoardConfig.h` as any of
the three -- rather than a list of backends, and `BoardConfig.h` static_asserts
that a board states at most one.

`AUDIO_VOLUME_MAX` is now `BOARD.audio.maxVolume`, set per-board in
`BoardProfile`. A board that gains a louder amplifier sets its own ceiling;
a board with no speaker path sets it to 0.

`Board::hasSound()` is `GUME_HAS_AUDIO`: a settings screen can offer the mute
switch and volume slider on any audio-capable board, not just codec boards.

**Register 0x32 is logarithmic and was being driven as if it were linear.**
Half a decibel per step, `0xBF` unity, `0x00` silence -- so scaling the
percentage onto the byte, which is what both this and `src/s3_diag.cpp` did,
is wrong by up to 25 dB. The default 60% was landing on -19.5 dB and the
console was nearly inaudible across a room; 100% would have been +32 dB and
clipped everything. `applyCodecVolume()` now converts a percentage of
amplitude (100% unity, 50% -6 dB, 10% -20 dB) and clamps at unity, and
`es8311VolumeReg()` in the probe is the same function -- **change one, change
both**, for the same reason the battery constants carry that rule: a bench
instrument that disagrees with the product about what "80" means is worse than
no instrument.

**IDF version dependency.** `I2S_DAC_BUILT_IN` and `i2s_set_dac_mode()` are
IDF 4.4 (Arduino core 2.0.17) APIs. They were removed in IDF 5.x. If the
platform is ever bumped, the DAC backend in `beginAudio()` and
`generateBlock()` and the whole of `src/audiodiag.cpp` need rewriting with the
new driver.

The knobs for retuning by ear are the formant numbers, the segment lengths,
`VOICE_PITCH_HZ` (monotone, which is what makes it robotic) and `NOISE_MAKEUP`
(raise it if the fricatives vanish under the vowels). Nothing else.

The codec register values and the three findings that are easy to get wrong --
active-low amplifier enable, MCLK from the APLL, ADC volume resetting to
minimum -- came from `env:s3diag` on real hardware and are documented at the
top of the file.

**`env:audiodiag`** is a standalone DAC bring-up probe (same pattern as
`batdiag`): builds `src/audiodiag.cpp` alone, no frame budget, no watchdog.
Sine wave at a configurable frequency, frequency sweep, volume steps and tone
bursts approximating each cue. The three questions it answers: does GPIO26
reach the speaker? Is output level audible? Where does distortion begin?

### RGB LED

LEDC PWM, common anode (inverted drive). `setRgbColor()` holds a colour, `pulseRgb()` shows one briefly, `tickRgb()` fades it and must be called every frame from the main loop. `beepOk()`/`beepError()` live in `BoardAudio.cpp` now and are the two cues that also pulse the LED; a screen wanting any other sound calls `playSound()` and pulses the LED itself if the moment deserves a colour. On a board with no audio path the pulse is the whole of the feedback, which is why the ones that replace a `beepOk()` keep it by hand. Whether the drive is inverted comes from `BOARD.rgb.commonAnode`, not from an assumption in the driver.

**On an I2C expander it is on or off.** When `BOARD.rgb.expander` is set -- the Attaky Core, red P1_0, green P1_1, blue P1_2 on the AW9523 at 0x59 -- `r`/`g`/`b` are that expander's ports and there is no LEDC: `Board::begin()` calls `BoardExpander::setupLed()` once the touch reset has been released (that release writes the whole bank), and `setRgb()`/`setRgbColor()` go through `driveExpanderRgb()`. A colour is shown as the channels at least half as bright as its brightest (`litAtAll()`), so grey is white and orange is red. Every write is a read-modify-write of the LED's own bits -- P1_3 beside them is the touch reset -- and a failed read writes nothing rather than guessing the rest of the byte. `rgbExpanderBits_` skips the bus entirely when the colour has not changed, which matters because the saver sets one per rally and the lock screen clears it on entry. GPIO mode, not the AW9523's LED current mode: dimming would be a register per channel and a second way to get the anode wrong, for a status light.

**`rgbExpanderBits_` records what the part took, never what was asked for.** `BoardExpander::driveLed()` returns whether the bus answered, and a write it did not take leaves the state `RGB_EXPANDER_UNKNOWN`, which matches no colour, so the next call reaches the bus again. It used to be recorded before the write: one failed transaction on the way into sleep left the LED lit, the firmware sure it was off, and every later "off" skipped as already done. **And off is confirmed, not trusted, while the panel sleeps**: `displaySleep()` calls `keepExpanderRgbDark(true)`, which forgets the remembered state so the part's own output register is read (and written only if it disagrees), and `tickRgb()` calls it again on every 100 ms poll of the sleeping loop -- no bus traffic once off is confirmed, and the retry when it is not. A pulse arriving while the panel sleeps is put out on the next poll; the find alert's is not, because it wakes the panel in the same frame. On every other board `keepExpanderRgbDark()` is an inline no-op.

The E32R28T-1's LED is red IO22, green IO16, blue IO17, per the vendor's table; IO4 is the amplifier enable, active low. It used to be declared `rgb.r = 16`, `rgb.g = 4` as "crossed" -- the evidence for that was IO16 being green, and driving IO4 as an LED held the amplifier in shutdown.

### Wi-Fi and time

Wi-Fi exists only to set the clock. `tickTimeSync()` drives a non-blocking `Idle → Connecting → Syncing → Synced` state machine from the main loop. The success-path automatic resync interval is a write-through cached setting, 1–24 hours with a 6-hour default; boot sync, manual sync and failure retries are separate. `ntpUdpProbe()` queries NTP over raw UDP and doubles as a diagnostic and a fallback that sets the clock directly when lwIP's SNTP never answers.

**The station is named `braino`, and it has to be named before it starts.**
Unnamed, the Arduino core calls it `esp32-` plus the low three bytes of the
Wi-Fi MAC and lwIP sends that to the router as DHCP option 12 -- half of an
identifier this repository's rules keep off the boot banner, in a name the
router records and System Info displays. `Board::WIFI_HOSTNAME` is the one
definition and `Board::applyWifiHostname()` the one setter.
`WiFi.setHostname()` only fills a buffer; the core hands it to the interface
inside `WiFi.mode()`, on a transition into station mode, and `mode()` returns
early when the mode is already the one asked for. So `Board::begin()` names
the station before any radio code runs, and every `WiFi.mode(WIFI_STA)` and
`WiFi.begin()` in the product is preceded by the call as well.
`tools/check_privacy.py` fails on a start without it, on a second setter and
on a name that is not a literal. A console is told apart by
`Board::deviceId()`, never by this name. Whether the name reaches a given
router can only be read off that router's client list or a packet capture.

**These, the opt-in BLE beacon and the update check in `BoardUpdate.cpp` are the only outbound flows the firmware has, and that list is closed.** No analytics, no usage or crash reporting, no other HTTP/UDP/DNS request, and nothing transmitted that carries a player's name, profile name, score, progress or typing. `BoardNetwork.cpp` and `BoardUpdate.cpp` are where such a thing would be added, so they are where it gets refused: a fifth flow needs agreement in an issue before the code exists. See [CONTRIBUTING.md](../../CONTRIBUTING.md#no-data-collection).

The update check is the worked example of how that agreement is supposed to go. It was argued as a change to what the product promises, not reviewed as a feature, and it was then built so the promise is checked rather than asserted: the request carries nothing about the device, and the address shown to the owner is compiled in rather than read out of the response. `tools/check_privacy.py` fails on either being lost.

Credentials and the NTP hot-path settings are cached in RAM because the state machine polls them at ~27 Hz and hitting NVS that often wasted cycles and flooded the log with `nvs_get_str NOT_FOUND`.

Timezone is stored in **minutes** to support :30 and :45 zones, and comes from a named POSIX zone or a public-IP lookup — DHCP options 100/101 exist but are essentially never implemented by routers.

## BoardButton.cpp

The BOOT key — the only input on these boards that is not the touchscreen. A GPIO with an internal pull-up, sampled once per frame by the runtime, edge-detected in `pollBootButton()`. No interrupt and no task: the debounce *is* the sampling rate, and a tactile switch settles well inside a 20 ms frame. Poll it faster and it will need a real one.

Two things it deliberately does not do. It never reads the key during boot — BOOT is a strapping pin, the ROM samples it at reset to choose serial download mode, and that decision is made before `setup()` runs. And it does not time the press: a hold is not a gesture this firmware recognises, and a `heldMs` nobody reads would claim otherwise. The runtime acts on the press edge, so anyone adding a hold has to move that to the release edge first — you cannot both go home on the press and later decide the same press was the start of something longer.

`PIN_NONE` is a supported profile. The event is all-false on a board with no key, so callers need no `hasBootButton()` guard of their own.

## Code only some boards carry

Five macros say what a board's image carries beyond the common firmware, and every one of them is defined in a board header and nowhere else -- today only `attaky-core.h`: `GUME_TOUCH_CAPACITIVE` (Wire, the FT6336U), `GUME_HAS_IO_EXPANDER` (`BoardExpander.cpp`), `GUME_HAS_NAV_KEYS` (`BoardKeys.cpp`), `GUME_HAS_KEYBOARD` and `GUME_HAS_ATTAKY_MODULES` (`modules/`, `BoardModules.cpp`). `BoardConfig.h` defaults each to 0 and static_asserts that the profile agrees with it in both directions, and the board header writes the dependent profile fields from the macro, as it writes `TouchKind`.

**Attaky code compiles only into the Attaky image.** That is checked, not intended: on every other product environment `xtensa-esp32-elf-nm -C` on `firmware.elf` shows none of `pollControls`, `pollTypedChar`, `typedChar`, `selectorFrame`, `Focus::`, `BoardExpander`, `sampleModuleBattery`, `Modules::`, `i2sUp`, and its RAM is `dev`'s. The way to keep it true is **whole files that compile to nothing** -- `#if GUME_HAS_X` round the entire body -- with the declarations they need behind the same macro, and an inline no-op in the header where shared code has to call across (`Modules::probeAll()`, `Modules::allows()`, `Modules::panelSleep()`, `Board::keepExpanderRgbDark()`, `hostTypedChar()` in `Game.h`, the `Focus::note*()` calls in `TftRenderer.h`, `SystemInfoApp::addModuleRows()`, `AboutApp::renderModules()`). A shared file that grows a scattered `#if` is the thing to avoid; a profile `if (BOARD.hasX())` is not a substitute, because both arms of it still compile and every declaration it needs stays in every class.

## The Attaky Core (experimental): standing rules

*Moved here from the root `CLAUDE.md`'s architecture invariants, verbatim.
The Attaky Core is `custom_env_kind = experimental`: built by CI, never
published. `docs/boards/Attaky-Core.md` is the board itself.*

- **An LED can live on an I2C expander, and then it is on or off.**
  `RgbLedProfile::expander` is 0 for GPIOs (LEDC PWM, every board but one)
  or an AW9523's address, with `r`/`g`/`b` as its ports -- the Attaky Core's
  P1_0..P1_2. `BoardExpander::driveLed()` reads, modifies and writes only
  those bits, because P1_3 beside them is the touch reset, and
  `BoardConfig.h` refuses an LED port that is the reset. A colour becomes
  the channels at least half as bright as its brightest, and the bus is
  written only when that changes -- the saver sets a colour per rally.
  **What is remembered is what the part took, not what was asked**:
  `driveLed()` says whether the bus answered, and a write it did not take
  leaves the state unknown, so the next call tries again. Recorded before
  the write, one failed transaction on the way into sleep left the LED lit
  and the firmware sure it was off. While the panel sleeps, off is read
  back from the part and held (`Board::keepExpanderRgbDark()`, from
  `displaySleep()` and every `tickRgb()`), at no bus cost once confirmed.
- **The firmware's lights are out while the panel sleeps -- on the Attaky
  Core, where there is more than one.** The status LED as above; and each
  module that is clipped on is told through `Modules::Driver::panelSleep`,
  called from `Board::displaySleep()` and `displayWake()`, so the keyboard
  puts its caps-lock light out and brings it back on wake. The battery
  pack's light is the charging hardware's and no line of the host's reaches
  it; do not go looking for one. The gamepad's LEDs are left inputs, and
  the probe logs what those lines read (`[gamepad] leds as inputs: ...`)
  -- **they are not to be driven until that has been read on a device**.
- **An expander the firmware drives no port of is parked, not ignored.**
  An AW9523 strapped to 0x58 powers up with all sixteen ports outputs
  driving LOW, and the Core's is wired to the other chips' interrupt
  lines, so a firmware that never wrote to it held them all low.
  `BoardProfile::parked.parkedExpander` names it (0 on every other board),
  `Board::begin()` calls `BoardExpander::park()` before the touch reset is
  released -- every port an input, the part's own interrupt output off --
  and logs `[expander] 0x58 inputs P0=0x.. P1=0x..`. See `src/hal/CLAUDE.md`.
- **Neither the SD slot nor the speaker is brought up over another
  module's lines.** On the Attaky Core three Expand modules share IO5-8, and
  the speaker's I2S (IO17/IO18/IO21) is the LoRa/GPS module's GPS UART and
  radio interrupt and the FM/AM module's I2S; this firmware is built for
  Media I/O. The three are the module table's `expand` slot, decided once
  in `begin()` by `Modules::probeAll()`, before `beginAudio()` and
  `mountSd()`, from `ExpandProbeProfile`: an address write to the FM/AM
  tuner on that module's own bus, then a passive `GPS_LISTEN_MS` (1.5 s)
  listen for a checksummed NMEA sentence on IO17, the UART's receiver only.
  Either found, or the radio's bus failing to start, leaves the slot down
  and the I2S backend uninstalled for the boot -- `mountSd()` and
  `beginAudio()` ask `Modules::allows()` for Storage and Sound, which only
  Media I/O provides -- and logs
  `[expand] module=... sd=skipped audio=skipped`. Silent is the state an I2S
  install failure already produced, so nothing downstream is new. Media I/O
  has no host-readable identity, so finding neither is the only answer that
  brings either up. Nothing it touches is driven, and it must stay ahead of
  `beginAudio()`: on Media I/O, IO17 is the amplifier's bit clock.
- **A power key's short press is the Lock button.** `KeysProfile::keyPower`
  names its port (P0_7 on the Attaky Core) and `pollNavKey()` reports it as
  `NavKey::Power`, which the runtime consumes above the active screen --
  on the launcher too -- and turns into `lockAndSleepNow()`; no screen ever
  sees it. It follows BOOT's rules: it counts as activity, wakes a sleeping
  panel and dismisses the saver like any key, does nothing on the lock
  screen, and reaches nothing the padlock does not. The long press switches
  the power in hardware and never reaches the firmware -- build nothing on
  it.
- **A physical keyboard types into the text fields, and while it does, the
  on-screen keypad is off -- one tap away, never gone.** `KeyboardProfile` names the two halves
  of Attaky's clip-on keyboard and spells out each half's 25 positions;
  the module table probes both at boot and the keyboard is off unless both
  answer, because the module is detachable. `Board::pollTypedChar()` reads one
  register a half per frame while nobody types -- the rows are held low,
  so any key shows on the columns -- and walks the rows only while a key
  is down. The runtime hands the character to the active screen through
  `GameHost::typedChar()`, beside `navKey()`. Five fields take it: a
  player's name and a Nearby peer's (through `Ui::Keypad::fromTyped()`,
  so they accept exactly the keypad's capitals, digits and space), both
  admin PIN pads (digits, Delete, Enter), and the Wi-Fi password (what was
  typed, case and punctuation included). The keyboard's one Shift is
  printed as a phone's up arrow and is pressed like one: tapped alone it
  toggles caps lock, held with a letter it shifts that letter. Caps lock is
  shown on the left half's own LED, which shares the AW9523's P1 with the
  matrix rows -- so every row write carries the LED bits (`rowsLow()`), or
  the scan would put the light out on the first key. **A screen must handle a typed
  character apart from the touch**: on its frame the touch coordinates are
  the last touch's, and running the hit tests would press that again.
  Each screen reads it through `hostTypedChar()`, a constant 0 where
  `GUME_HAS_KEYBOARD` is off, so the boards without one carry none of this.
  **With the keyboard found at boot, those five fields draw no keypad**
  (`ui/TypingPanel.h`): the field is drawn larger, the PIN's dots too, with
  "Type on the keyboard" under it, and only what is not a key stays -- OK or
  JOIN, Cancel or Back, Show. No DEL: the keyboard has Delete. Three things
  are load-bearing. **Each screen keeps a `Ui::KeypadChoice`, decided as
  the field opens**, so the choice is per visit and the next visit asks
  again. **A keyboard button on every one of them brings the keypad back
  for that visit** -- the module comes off and boot is the only time
  anybody looks, so without it a field could be reachable only by key.
  **A field that takes what the keyboard cannot type keeps it on screen**:
  the keyboard has letters, digits, space and only `,` `.` `#` of the
  punctuation, so the Wi-Fi password keeps the on-screen symbol page's three
  rows where the keys were (`WifiPassword.cpp`); the names and PINs take
  nothing the keyboard lacks. A new field, or a keyboard with a different
  layout, has to be checked the same way. **And the
  keypad comes back by itself when the keyboard stops answering**:
  `Modules::keyboardAnswering()` counts the per-frame scan's own failures
  (five in a row is gone), and each screen asks `KeypadChoice::lost()`
  every frame and repaints fully on a yes. Elsewhere `KeypadChoice` is an
  empty class with a constant-false `hidden()` and the drawing helpers are
  inline no-ops, so every screen's branch folds away.
- **The profile says where a module could be; boot decides whether it is
  there.** A board that takes plug-in modules -- the Attaky Core's keyboard,
  gamepad, Power module and Expand modules all come off -- states in its
  profile what it would need to know about each one it can carry, and
  nothing about whether one is attached. `src/hal/modules/` holds one driver
  file per module: a `Modules::Driver` (name, the `ModuleCapability` bits it
  provides, a boot probe, per-frame or battery hooks, a slot for modules that
  share lines) and a `Modules::Registration` that puts it in the table from
  its own file. `Modules::probeAll()` runs every probe once in
  `Board::begin()`, and nothing a module provides is used unless its probe
  found it. **Supporting a new module is one driver file plus its facts in
  the profile**: the boot sequence, the navigation-key stream, typing, the
  battery path, the SD and speaker gates, System Info's Board tab and
  About's Modules page all read the table, so none of them changes. See
  `src/hal/CLAUDE.md`.
- **Attaky code compiles only into the Attaky image.** Five board-header
  macros -- `GUME_TOUCH_CAPACITIVE`, `GUME_HAS_IO_EXPANDER`,
  `GUME_HAS_NAV_KEYS`, `GUME_HAS_KEYBOARD`, `GUME_HAS_ATTAKY_MODULES` -- are
  defined only in `include/boards/attaky-core.h` (the first also in
  `fnk0104b.h`), default to 0 in `BoardConfig.h`, which static_asserts the
  profile agrees with them both ways, and preprocess out everything behind
  them: whole files, `Board`'s members and accessors, `GameHost`'s
  `typedChar()` virtual, the runtime's key routing and its selector, the
  focus recorder, the screens' typed paths, the gauge and the module
  table. **Prefer a whole file that compiles to nothing** over an `#if` in a
  shared file, and where shared code must call across, an inline no-op
  beside the declaration (`hostTypedChar()`, `Modules::allows()`, the
  recorder's `Focus::note*()` calls in `TftRenderer`). A `BOARD.hasX()`
  test is not enough -- both arms still compile and its declarations stay
  in every class. Checked rather than hoped: on each other product
  environment `xtensa-esp32-elf-nm -C firmware.elf` finds none of
  `pollControls`, `pollTypedChar`, `typedChar`, `selectorFrame`,
  `Focus::`, `BoardExpander`, `sampleModuleBattery`, `Modules::` or
  `i2sUp`, and it carries no RAM beyond what the shared code costs.
  **One stated exception: `src/ui/Stops.{h,cpp}`**, what a game says about
  its board (`Ui::setBoardStops()`) and a card over it. It compiles on every
  board because the PC and RG40XX V builds read it (`HostTftFocus.cpp`), and
  they define no `GUME_HAS_NAV_KEYS` -- their selector is the host's. It is
  two rectangles and a flag, assigned per frame and drawn by nothing.

## modules/ + BoardModules.cpp -- what is clipped on

**The profile says where it could be; boot decides whether it is there.** A board that takes plug-in modules states in its profile everything about each one it *can* carry -- addresses, ports, layouts -- and nothing about whether one is attached, because that is not a fact about the board. Each module has a driver in `modules/`, one file a module, and `Modules::probeAll()` runs every driver's probe once in `Board::begin()`, after the touch bus is up and before `beginAudio()` and `mountSd()`. A module is used only if its probe found it.

A driver is a `Modules::Driver` constant -- name, log tag, order, the `ModuleCapability` bits it provides, a slot, and up to five hooks -- and a `Modules::Registration` beside it at namespace scope. The registration is a static object, so **the driver's own file puts it in the table**: there is no list to edit. Supporting a new module is that file plus its facts in the profile; the boot sequence, the input stream, the battery path, System Info and About all read the table, so none of them changes. The table is a fixed array of pointers (`MAX_DRIVERS` 12) kept in `order` as registrations arrive; it allocates nothing.

What a capability does:

- `NavKeys` -- `heldControls()` adds what the module holds to `pollControls()`'s set every frame, beside the board's own keys and **before** `turnForScreen()`. So a gamepad and the Core's keys are one held set above the HAL, turned for the rotation the same way, and nothing upstream can tell them apart.
- `Typing` -- `typedChar()` is `pollTypedChar()`'s answer.
- `Battery` -- `readBattery()` is what `Board::sampleModuleBattery()` publishes, from the battery task.
- `Storage`, `Sound` -- `mountSd()` and the I2S branch of `beginAudio()` ask `Modules::allows()`, which is a constant true on a board without a table.

**Slots are for modules that share lines.** In a slot the first driver by `order` whose probe answers is the module there, and the rest are not asked; a probe that could not run (`Probe::Unknown`) decides the slot as unknown and nothing in it is used, because a guess about who owns a line is how a line gets driven into somebody else's chip. A `byElimination` driver -- Media I/O, which has no identity a host can read -- is present only when nothing else in its slot was found. Each slot logs one line, `[expand] module=<tag> sd=allowed|skipped audio=allowed|skipped`, which is the line the Expand check always printed.

**A `live` driver is asked again by its own hook rather than trusted from boot**: the battery module, which can come off while the console runs from USB. Its presence follows `readBattery()`'s answer, so System Info and About follow it too.

**A module with a light puts it out while the panel sleeps.** `panelSleep(bool asleep)` is the fifth hook, optional like the rest: `Board::displaySleep()` and `displayWake()` call `Modules::panelSleep()`, which tells every module that is present, on the loop task and between frames. A locked console that has been put down shows no light of the firmware's, and a module is the only code that knows where its own light is. A light the host has no line to -- the Power module's charge light, which is the charging hardware's -- is not the firmware's and gets no hook. On a board with no table `Modules::panelSleep()` is an inline no-op.

**A module with a light can show which one it is.** `lightSteps` and `lightStep(step, on, name, cap)` are the sixth hook, for the serial console's `lights` (`engine/AppRuntimeLights.cpp`): one step for each thing the driver can do to a light of its own, done or undone on request, and named. The keyboard's six are its two halves' three channels driven low, the bit riding in `rowsLow()` beside the caps-lock light's so that a scan does not put it out. The gamepad's twelve are each channel driven low and then high, and they are the only place those ports are outputs: the driver itself leaves them inputs until somebody has watched which level lights them. A module with no light the host can reach gives 0 and `nullptr`.

The drivers, today all Attaky's:

- **`KeyboardModule.cpp`** -- Attaky's two-half keyboard, each half an AW9523 scanning a 5x5 matrix. `KeyboardProfile` gives both addresses and a 25-character layout per half, with four codes for keys that type nothing (`^` Shift, `<` Delete, `>` Enter, `~` unused). The probe sets both halves up with every row driven LOW and says `[keyboard] attached|absent`; both or neither. **Idle, it costs one register read a half**: with every row low any key pulls its column down, so the frame's read stops there when they are all high, and only a key down makes it walk the rows -- about a millisecond. One character a frame: a letter (upper while Shift is held), a digit, space or the punctuation as itself, `'\b'` for Delete (repeating while held), `'\n'` for Enter. Its AW9523 writes are its own, not `BoardExpander`'s: the module is not the board. **The caps-lock light shares P1 with the matrix rows**, so it is never written alone: `rowsLow()` is the one statement of that register between scans, every row write carries it, and `panelSleep()` changes what it says (light out while the panel sleeps, back on wake if caps lock is still on) and writes the whole value. A light write the bus did not take is retried from the next frame's `typedChar()`, since an idle scan only reads. **It also says whether it is still there**: `Modules::keyboardAnswering()` is false once five scans in a row have failed -- the module coming off, a tenth of a second -- and true again on the next scan that succeeds, logged as `[keyboard] not answering` / `answering again`. It is a count of the scan that runs anyway, so it costs nothing; `ui/TypingPanel.h` brings a text field's on-screen keypad back on it. The module is not re-probed: put back while running, it answers again but stays as the probe set it only if it kept its power.
- **`GamepadModule.cpp`** -- Attaky's Control: Gamepad (Bottom), a TCA9555 a half at 0x22 and 0x26. `GamepadProfile` is the whole module as a layout string a half, P0_0..P0_7 (`SODRLU<{` and `}>XABYMT`): the map is Attaky's own, from their Retro-Go patch for this module, not their datasheet, which named only six keys. All active low on the part's own pull-ups. The probe writes both halves' configuration registers to all-inputs -- which is also what leaves each half's RGB LED off, if it is wired as Attaky document it (active low on P1_5..P1_7) -- then logs `[gamepad] leds as inputs: left P1=0x.. right P1=0x..`, each half's port-1 input register: all three top bits high is the documented wiring at rest, and a bit reading low is a channel wired the other way round, glowing through the part's pull-up. **Those ports are not driven until that line has been read on a device**; a light was seen on the module with the console locked, and a guess at the polarity is how it would be lit for certain. The frame read is one input register a half, held or idle, because every key is on port 0. **Seen on a Cyber Deck Plus**: the D-pad on left bits 2-5, and bits 0, 1, 6 and 7 of both halves answering; which key does what is `hal/ControlJobs.h`'s business, not this file's.
- **`FuelGaugeModule.cpp`** -- the Power module's gauge, whichever of
  MAX17048, INA226 and INA219 is fitted (see Battery sensing).
- **`ExpandFmAm.cpp`, `ExpandLoraGps.cpp`, `ExpandMediaIo.cpp`** -- the three Expand modules on the `expand` slot, which share IO5-8 and the speaker's I2S lines. Passive-first, after Attaky's `shared/module-detection-and-safe-boot.md`: FM/AM (order 40) is one address write to its tuner (0x64) on that module's private bus (`Wire1`, IO42/IO41), torn down after and left as inputs, and a bus that will not start is `Unknown`; LoRa/GPS (order 50) attaches the UART's receiver alone to IO17 for `GPS_LISTEN_MS` (1.5 s) and wants one NMEA sentence whose checksum adds up -- not just a `$`, because on Media I/O that line is the amplifier's idle bit clock -- and leaves IO17 `INPUT_PULLDOWN`, Attaky's park for it; Media I/O (order 60) is by elimination and provides `Storage` and `Sound`. Neither of the other two provides anything: finding one is what keeps the SD slot and the speaker down. **Keep `probeAll()` ahead of `beginAudio()`**: an I2S backend clocking IO17 while the GPS probe listens would be both a wrong answer and a noise through the speaker.

Everything a driver does at boot is on the loop task; `heldControls()` and `typedChar()` run on it every frame; `readBattery()` runs on the battery task, sharing the bus with touch under `TwoWire`'s own lock.

## BoardKeys.cpp + BoardExpander.cpp + HeldControls.h + ControlJobs.h

The keys, on the boards that have them -- today the Attaky Core, whose own keys are ports of an AW9523 on the touch bus, plus whatever a module with `NavKeys` holds. `Board::pollControls()` reads one input register per frame from the loop, beside the BOOT key and for the same reason touch is read there, and returns **what is held** as a `HeldControls`: the buttons as `1 << ControlJobs::Control`, the directions as four bits, and the power key. Presses, releases and repeats are the selector's (`engine/AppRuntimeSelector.cpp`), as they are the Linux host's. A failed read keeps the last state, so a bus glitch cannot invent a press.

`KeysProfile` names the Core's ports: the D-pad, the centre key -- Select, or A on a deck with no gamepad, so a press still exists -- the top-left shoulder (L1) and `keyPower`. The power key's short press comes back as `held.power` and the runtime takes it as Lock; a long press that switches the power in hardware is invisible to this code by construction. **The Core's own keys have not yet been seen to answer on a device**: on the first Cyber Deck Plus the expander's port never changed while its strip was pressed. `[keys] core 0x59 P0=..` is logged on every change of that port, and is the line that will show them.

`ControlJobs.h` decides what each control is for from the controls the console has, top to bottom by how much a job is missed -- the Linux host's table, moved here so both consoles share it. `Board::controlsPresent()` says what this one has. **`maskOf()`, not `bit()`**: Arduino defines `bit` as a macro.

**The direction comes back in screen terms.** The keys are stated as they point at the board's landscape rotation, and `turnForScreen()` turns the held set by the rotation that is current, using the convention `pollTouch()`'s transform establishes -- each step of TFT_eSPI rotation is a quarter-turn of the picture anticlockwise. No screen corrects for rotation. **Seen right at the landscape rotation only**: if portrait or flipped is wrong, the sign in that one function is the whole fix.

`BoardExpander.cpp` holds the Core's own AW9523 writes -- the status LED's (see RGB LED above), and at boot the touch reset (`driveTouchReset()`, which also leaves every other port an input -- so the Core's keys need nothing more), `allInputs()` for a key expander that is not the reset's, and `park()` for an expander the firmware drives no port of. `GUME_HAS_IO_EXPANDER` only.

**An expander nobody drives still has to be written once.** An AW9523 does not power up as inputs: strapped to 0x58 it comes up with all sixteen ports outputs driving LOW (datasheet Table 1), and the Core's 0x58 is where the other chips' interrupt lines arrive -- touch on P0_4, the gauge's alert on P0_2, the IMU on P1_5 and P1_6, the 0x59 expander's own interrupt on P1_7, the gamepad's halves on P1_4 and P0_7, the keyboard's left half on P0_1. The firmware used to leave that part alone, which meant holding every one of those lines low against its owner's output or pull-up for as long as it ran. `BoardProfile::parked.parkedExpander` names such a part (0 on every other board; `BoardConfig.h` refuses the address of an expander the firmware does drive), and `Board::begin()` calls `BoardExpander::park()` on it as soon as the bus is up and **before the touch reset is released**, since one of those lines is the touch controller's interrupt. `park()` is `allInputs()` plus the part's interrupt enables switched off (0x06 and 0x07 to 0xFF): interrupts fire only for inputs, so parking is what would start them, and a latched INTN is a line held low until its input registers are read, which nothing here does. It then logs `[expander] 0x58 inputs P0=0x.. P1=0x..`, both input registers, which is where the levels of those lines can be read on a bench (`not answering` if the part did not). **Leaving a part alone is not leaving its lines alone**: a new expander in a profile is either driven or parked.

## Clock.{h,cpp}

Time/date formatting and sync-state helpers over the ESP32 RTC.

## Battery sensing

A board describes its battery in one of two shapes, never both (`BoardConfig.h`
asserts it): an ADC sense line behind a divider, which is everything below, or
a **gauge on I2C** -- `BOARD.battery.fuelGaugeAddress`, which only the Attaky
Core has. Its Power module carries a MAX17048 (0x36), an INA219 or an INA226
(0x40/0x44/0x45) depending on which module it is, and `FuelGaugeModule.cpp`
identifies the part by a register only it answers that way; an INA's
percentage comes from the LiPo curve, the MAX17048's from itself. `hasBatterySense()` is
true for either; `hasBatteryAdc()` and `hasFuelGauge()` say which, and
anything that touches the ADC pin keys on the first.

**The fuel gauge rides the same task and publishes the same snapshot.**
`sampleModuleBattery()` (`BoardModules.cpp`) replaces the ADC branch of
`sampleBattery()` on such a board: it asks the module table, whose
`FuelGaugeModule.cpp` reads VCELL (0x02, 78.125 uV per LSB) and SOC (0x04,
whole percent in the high byte, 1/256 in the low), and feeds SOC to
`settleDisplayPercent()`, the deadband the ADC path uses too. The gauge is a
`live` module: asked every sample whatever boot found, and its presence on the
System Info and About lists follows the answer. There is no LiPo curve and no gauge filter on
this path -- they exist to make a percentage out of a noisy voltage, and the
chip has already done that with a better model. SOC above 100 (it can, just
after a full charge) is clamped. Four things are load-bearing:

- **A gauge that does not answer publishes what a board with no sense line
  publishes**: a zeroed sample and -1. The module is detachable, so a NACK is
  an ordinary state, and the two tempting answers are both wrong -- a zeroed
  SOC is 0% and raises the critical banner on a console running from USB, and
  holding the last reading claims a charge nobody is measuring. Every reader
  already treats -1 as "nothing to show". `[battery] fuel gauge at 0x36
  answering|not answering` is logged on each change, from the task.
- **It shares the bus with touch, from another task.** The battery task is on
  core 0, `pollTouch()` on the loop on core 1. Arduino-ESP32 2.0's `TwoWire`
  holds its own mutex from `beginTransmission()` through the repeated-start
  `requestFrom()`, so the two transactions cannot interleave; the cost to a
  frame is waiting out one few-byte gauge read at 400 kHz. Do not add a second
  lock, and do not read the gauge from the loop instead.
- **The monitor starts after Wire is up**, later in `Board::begin()` than on an
  ADC board, because the first sample is synchronous and the bus is not
  brought up until the touch reset is released. ADC boards still start it
  where they always did.
- **Everything is under `#if GUME_HAS_ATTAKY_MODULES`**, so no other image
  carries a line of it -- see "Code only some boards carry". A board that
  names a gauge without the macro, or the macro without a module, is refused
  by `BoardConfig.h`.

The rest of this section is the ADC path.

`BOARD.battery.adcPin` is GPIO34 on this board = **ADC1**_CH6, and that matters: ADC2 is unusable while Wi-Fi is associated, and this radio comes up for NTP. Don't move battery sensing to an ADC2 pin.

**Sampling runs on its own task and never on a render path.** `sampleBattery()` is called every `BATTERY_SAMPLE_MS` (2s) by a priority-1 task pinned to core 0 — the same shape as Watchdog's monitor, and away from the Arduino loop task on core 1. It takes the eight ADC reads, advances both filters, applies the display deadband, and publishes `batteryPublished_` under a `portMUX`. Every accessor — `readBatteryTelemetry()`, `getBatteryVoltage()`, `getBatteryPercent()`, `isBatteryLow()`, `isBatteryCritical()` — does nothing but copy that snapshot. **Keep it that way.**

This has been wrong twice, in the same direction both times. First each accessor ran its own 10-sample conversion with a `delay(1)` between samples, and `Ui::drawTopBar()` calls two of them: every top bar cost ~20ms of blocking delay, and the System Info board tab ~30ms per repaint. Then it became a 2s cache, which fixed the average and not the frame — the first caller after the cache expired still paid for the conversion, and that caller was almost always a top bar being drawn. A cache makes the cost rare; it does not move it off the render path. Only the task does.

`Board::begin()` calls `beginBatteryMonitor()`, which takes one sample synchronously before starting the task, so the first screen drawn has a real reading and the gauge filter is primed rather than ramping up from zero. On a board with no sense line it publishes the zeroed snapshot and starts nothing.

Conversion goes through `esp_adc_cal`, not `raw / 4095 * 3.3`. At 11dB the ESP32's ADC is nominally 0–3.1V but only linear to ~2.45V, and its reference varies 1000–1200mV part to part; the eFuse calibration corrects both. The old comment claiming "3.3V reference with 11dB attenuation" described two mutually exclusive things.

Percentage uses a piecewise LiPo discharge curve. A linear 3.2–4.2V map reads roughly 20 points high through the middle, because a cell sits near 3.7V for most of its life.

**The displayed percentage carries a deadband, because one percent is finer than this hardware can resolve.** On the mid-discharge plateau the curve spends 10 points on 20mV — 2mV per point — while the divider halves the cell before the ADC sees it and one count at 11dB is worth over a millivolt. So a single count of noise is most of a percentage point, and the plateau is where the pack spends most of its life. The gauge filter kills the fast noise but cannot stop a value resting on a boundary from crossing it, and with the BLE beacon advertising its supply ripple made that continuous. `PERCENT_DEADBAND` (2) holds the shown number until it has genuinely moved; both endpoints are exempt, because "100%" and "0%" are the two readings anybody acts on. It applies to the percentage only — the voltage, the charge verdict and the filters are untouched.

Every crossing invalidates the header, which is why this mattered enough to fix twice over: see `Game::renderChrome()` for the other half, which stopped a 22-pixel badge from repainting the whole panel.

**There is no `isBatteryPresent()`, and there must not be one.** It used to exist and was a constant `true`. Measured on hardware 2026-08-24 with `batdiag` (8s averaged): pack + USB **4.224 V**, USB with **no pack 4.159 V**, pack alone **4.066 V**. The no-pack case sits *between* the other two, because the TP4054 holds BAT at float voltage with or without a cell — so no threshold separates them in either direction. This is measured, not inferred; do not reintroduce the question in a new form or retune a constant to answer it.

`V_SENSOR_MAX` (4.50V) is the plausibility ceiling that replaced `V_NO_BATTERY` (4.35V). It means **the ADC is faulty**, not "no pack": 4.35V was above anything this board produces, so it never fired, which left `getBatteryPercent()` unable to return -1 and the blank-digit rendering in `Ui::drawBatteryBadge` unreachable. With no pack the gauge reads *high*, near full.

Still assumed and still needing a meter: `DIVIDER_RATIO` (100k/100k).

## Where the battery went: PowerProbe, PowerMonitor, PowerLedger

Owners report the charge going down fast, and until this nothing could say why:
the gauge says how much is left, not what took it. The diagnostic is three
pieces, and only the first differs between platforms.

- **`PowerProbe.h` is the interface.** `read()` fills a `PowerReading` with what
  this platform can see, on the monitor's task. **To add a platform, implement
  it in one `.cpp` only that platform builds and define `platformPowerProbe()`.**
  An unanswered field stays at its default and is shown as not measured, never
  as zero, because a current sensor and a voltage divider differ by an order of
  magnitude in what they can say and the rest of the code must not care which
  it got.
- **`PowerMonitor` is the recorder.** A task every `SAMPLE_MS` (10s) reads the
  probe, fills what the probe left open from the firmware's own gauge and loop
  load, and books the time to the state `BrainoApp::tickPowerMonitor()` last
  published. System Info's Batt tab, the console's `power` command and the
  `[power]` log lines (one a minute, the whole table every ten) read the same
  ledger.
- **`PowerLedger` is the analysis**, and it is pure: no Arduino, no clock, tested
  by `test/host/power_ledger_test.cpp`. Time is divided between states (awake,
  saver, lock, asleep; the lamp's band; Wi-Fi, Bluetooth, case lights) and each
  state collects what it cost. **The measurement is comparing two rows that
  differ in one respect.**

Three things about the numbers:

- **Percentages are summed as deltas, so noise cancels.** A flicker nets to zero
  and the sum telescopes to the real change. A step landing just after a state
  change is charged to the new state, which is why a rate is withheld until a
  state has `MIN_RATE_SECONDS` (20 minutes) behind it. Current is reported at
  once.
- **On these boards it is a slope and nothing more.** No current sensor and no
  charge line, so a board on USB looks like one that is not draining
  (`percentDrop` falling to nothing), and `onCharger` stays unknown rather than
  being inferred, for the reason in "No charge detection" below.
- **It runs from boot, not behind a switch**, because a report of a flat battery
  arrives after the fact. It costs a few KB of RAM and a read every ten seconds.
  The ledger is RAM and starts over on a reboot, so a console that went completely
  flat has lost it; `Start over` on the tab is for a clean before and after. It
  stores nothing in NVS, and a read every ten seconds is the reason it can be left
  on: writing flash at that rate would be its own wear problem.

`lampPercent()` is on `Board` for this: `brightness()` is the stored setting and
an NVS read, and it stays at the old value while the panel sleeps.

## SystemSuspend: Deep sleep, where a platform can

Panel sleep keeps the processor up so a touch can wake it. Deep sleep stops
the whole device, so it can exist only where something other than the
processor can start it again. `SystemSuspend.h` is the interface, shaped like
`PowerProbe`: one `.cpp` per platform. `SystemSuspendEsp32.cpp` says no --
these boards wire no wake source (the touch controllers raise nothing the RTC
can see, BOOT is a strapping pin), and a deep sleep nothing can end is a
device that looks dead. So on a board the Settings row is not drawn and the
console refuses `set deep_sleep`; do not "add" it with esp_deep_sleep and a
timer, because a console that wakes itself to find nobody there has saved
nothing. The host's version suspends a Linux handheld to RAM
(`host/src/HostSuspend.cpp`).

The runtime owns when (`BrainoApp::tickDeepSleep()`,
`SystemSuspend::AFTER_ASLEEP_MS`), the platform owns how and may decline.
`enter()` blocks for as long as the device sleeps, so its caller holds a
`Watchdog::Pause`.

## SleepPower: what panel sleep switches off besides the panel

`SleepPower.h`, shaped like `SystemSuspend`: `SleepPowerEsp32.cpp` lowers the
processor to 80 MHz (`lowerClock()` / `restoreClock()`) and stops and starts
the Wi-Fi station (`stopStation()` / `startStation()`); the host's answers
no, since `HostSleep` does the Linux equivalent. The runtime decides when
(`enterSleep()`, `tickStationRest()`, `wakeFromSleep()`), and `Board::
restWifiForSleep()` / `wakeWifiFromSleep()` keep the time sync state machine
still while the station is down and set it going again after.

**Stop, never `WiFi.mode(WIFI_OFF)`.** The core's `mode(WIFI_OFF)` follows
`esp_wifi_stop()` with `esp_wifi_deinit()`, and a de-init beside a live
Bluetooth controller is the teardown that has panicked this device (see
BleBeacon below). `esp_wifi_stop()` keeps the driver and its netif. The
core's own started flag stays set, so `WiFi.getMode()` still reads STA:
`BleBeacon::tickRadio()` therefore keeps any deferred controller teardown
deferred, which is the safe direction, and the next `WiFi.mode(WIFI_STA)`
returns early as it always did. `esp_wifi_start()` raises STA_START, where
the core re-applies its stored modem-sleep choice -- which is why
`WifiApp::end()` must put that choice back to on.

## No charge detection

There is no charging state, and there should not be one again without a
charge-status line to read. The charger's CHRG pin is not wired to the ESP32,
so until 5.10.0 `getChargingState()` *inferred* charging from how the cell
voltage moved -- a step between samples, a voltage held above what a resting
cell reaches, a 45-second trend. It drove a lightning bolt in the battery badge
and silenced the low-battery warning, and it was a guess presented as a fact.
The badge now shows the percentage and nothing else.

`isBatteryLow()` / `isBatteryCritical()` are the percentage at or below 15% /
5%, full stop. On the charger the reading climbs back over the threshold within
a minute or so, which is what clears the warning. `env:batdiag` still has its
own copy of the old inference as a bench aid for watching a charge; it no
longer mirrors anything in the product.

### The built-in DAC backend, and four ways it produces perfect silence

`GUME_HAS_AUDIO_DAC` boards have no codec: GPIO26 is ESP32 DAC channel 2, the
I2S peripheral drives it via `I2S_DAC_BUILT_IN`, and an onboard 8002-series amp
does the rest. The synthesiser above is untouched by this -- `nextSample()`
produces mono int16 and knows nothing about which backend consumes it.

Four separate faults were found bringing this up on the E32R40T, and **each one
alone gives complete silence from a driver that reports success**. That is what
makes this path expensive to debug and worth writing down.

1. **`communication_format` must be `I2S_COMM_FORMAT_STAND_MSB`.** The framer
   is what latches samples into the DAC, and PCM short-frame sync does not
   present the word the way the DAC expects. Under `..._PCM_SHORT` the driver
   installs, every `i2s_write()` reports all bytes accepted, and nothing is
   audible. The codec path uses `STAND_I2S`, which is right for the ES8311 --
   the two are different questions, do not unify them.
2. **The channel enum names are inverted relative to the channel numbers.**
   `I2S_DAC_CHANNEL_RIGHT_EN` is DAC1/GPIO25; `..._LEFT_EN` is DAC2/GPIO26.
   `beginAudio()` derives the constant from `BOARD.audio.speakerPin` with a
   `static_assert`, so this cannot be got wrong by memory again.
3. **Do not call `i2s_set_pin(port, NULL)`.** It is the routing call most
   examples use, and it enables *both* DAC channels -- GPIO25 as well. On this
   board family GPIO25 is the resistive touch SPI clock, so it trades silence
   for broken touch. `i2s_set_dac_mode()` for one channel is necessary but,
   per 5.5.0, was not sufficient on the boards whose touch clock is GPIO25:
   that release shipped with only this call and still lost touch. So
   `beginAudio()` also powers the unused channel down and `rtc_gpio_deinit()`s
   its pad, and logs `[audio] GPIO25 released: rtc_mux a->b, dac_out c->d` --
   and `Board::begin()` re-applies the touch pins afterwards. On the first
   E32R28T-1 boot the log read `0->0` both ways: the pad was not claimed at that
   moment, so whether 5.5.0 lost it later, on first playback, is the question
   the owner's finger answers.
4. **The amplifier outlives generation.** `playing` goes false when the last
   sample is GENERATED, and the DMA still holds up to 96ms of it. Dropping the
   amp there eats the tail, and on the short cues that is the whole cue.
   `AMP_TAIL_MS` holds it, reset by `arm()`. Both backends do this. It is
   2000ms, not the DMA depth: switching the 8002's enable is a pop of its own,
   and at 150ms it fired around every piano note.

And one fault that is not silence but a click on both edges of every sound,
which is what "beeps all over the device" turned out to be:

5. **Idle is mid-scale, and `tx_desc_auto_clear` does not know that.** With
   auto-clear on, the driver fills an idle DMA with zero words -- silence for
   the codec, **0 V** for this DAC, whose silence is `0x8000`. So the speaker
   line dropped to ground as every sound ended and jumped back as the next
   began; recorded on the device as a click ~300ms apart around each piano
   note. Auto-clear is **off** on the DAC backend, and `fillDacIdle()` writes
   mid-scale into every DMA buffer once a sound ends (and at boot), so what an
   idle DMA replays is silence. Turn auto-clear back on here and the clicks
   return; forget the fill and the DMA replays the last sound's tail forever.

Separately, and on both backends, **a sound fades out as well as in**: a
spent script keeps its last waveform running while the gain slews to zero
(`RELEASE_FLOOR`, ~25ms), because stopping a sine at its crest is a step of
the full amplitude. Mute uses the same fade rather than cutting.
A rest inside a script fades the same way: it keeps the last sounding
segment's waveform running while the gain falls, because a rest that
outputs zero from its first sample is the same step (`AudioSynth.cpp`;
`test/host/audio_synth_test.cpp` fails on it). And a held note is held
again (`Sound::NoteHold`) every `SOUND_NOTE_REARM_MS`, well inside
`SOUND_NOTE_MS`, because the generator runs a DMA ahead of the speaker --
`BoardAudio.cpp` static_asserts the margin.

**The sample rate is not the same on both backends, and this is measured.** The
built-in DAC's clock divider cannot reach low rates: below 22050 Hz it wraps
rather than saturating, and the peripheral runs at some faster rate entirely.
Timed on an E32R32P with `env:audiodiag_e32r32p`, by measuring how long a
blocking `i2s_write()` of a known number of frames takes to drain:

| configured | actual | |
|---|---|---|
| 8000 | 44260 | 5.53x |
| 11025 | 25316 | 2.30x |
| 12000 | 31128 | 2.59x |
| **16000** | **88642** | **5.54x** |
| 22050 | 22053 | 1.00x |
| 24000 | 24006 | 1.00x |
| 32000 | 32000 | 1.00x |
| 44100 | 44077 | 1.00x |
| 48000 | 48048 | 1.00x |

So until 5.7.0 the synthesiser generated for 16 kHz while the hardware consumed
at nearly 89 kHz: every cue played in about a fifth of its length, an octave and
a half sharp. Nobody hears that as "too fast" -- it is heard as a click, or as a
speaker that is cutting out, which is how it was reported on the 3.2-inch and
4-inch boards. `AUDIO_RATE` is therefore **24000 on the DAC and 16000 on the
codec**, whose clock comes from the APLL and was never affected.

**`i2s_get_clk()` cannot be used to check this.** It returned the requested
value in all nine cases above, including the wrong ones: it reports what the
driver was asked for, not what the peripheral is doing. Only the drain timing
tells the truth, which is what the RATE page exists for.

One consequence of an 8-bit DAC, and it is not a defect: samples are written as
unsigned offset-binary (`sample + 32768`, only the high byte reaches the DAC).
The spoken boot phrase was previously documented here as not surviving the
quantisation. It does -- that was written while the rate was wrong, and it is
intelligible on a DAC board now. The codec board is 16-bit and still sounds
better; that gap is real and cannot be tuned away.

`env:audiodiag` is the probe. It gives an unambiguous A/B on one reset with no
panel and no button: a PWM burst (no DAC at all) proves speaker, amp and enable
line; an I2S tone proves the DAC path; a direct `dacWrite()` tone separates the
I2S framing from the DAC peripheral. Its first interactive page is now **RATE**,
which is silent, needs no ears, and reports the measurement above.

Two things about the probe that cost time to find. The direct-DAC test in
`setup()` has to uninstall the I2S driver to take the pad, and for a while it
never reinstalled it -- so every interactive page called `i2s_write()` on a
driver that was not there and faulted instantly (`LoadProhibited`,
`EXCVADDR: 0x00000018`). The log looked perfect right up to the reboot. And the
probe's own `SAMPLE_RATE` must track `AUDIO_RATE`: a bench instrument that
disagrees with the product about what it is measuring is worse than none, the
same rule the battery and codec-volume constants carry.

## BleBeacon.{h,cpp}

Opt-in, non-connectable BLE presence beacon in namespace `BleBeacon`. Off by default; `Board::bleBeaconEnabled()` / `setBleBeaconEnabled()` hold the switch in the application NVS namespace, so a factory reset clears it. `Board::begin()` calls `BleBeacon::begin()` which always builds the advertisement and only powers the radio when opted in.

**The invariant that matters here:** there is exactly one description of the outgoing advertisement. `Advertisement` is compiled by `buildPayload()` into a raw AD-structure buffer, `startRadio()` hands the controller *that buffer* via `NimBLEAdvertisementData::addData()` rather than the per-field helper setters, and the System Info BLE tab reads the same buffer back to display it. Never add a second, hand-written description of the payload to the UI — that is the only way the screen and the radio can drift apart. See `docs/BLE_BEACON_SPEC.md`.

`broadcasting()` returns `nullptr` unless the controller is actually advertising. UI must key off that rather than off the stored setting, which can read On while the radio failed to come up; nothing may be labelled as on air unless it is.

On air: Flags, Complete Local Name (`Braino-<id>`) and manufacturer data (company `0xFFFF`, `"BR"`, layout version 4, two MAC bytes, a flag byte) — 27 of the 31 legal bytes. The device id is the last two bytes of the factory BT MAC. Nothing profile-scoped is read by this module at all.

With Nearby play on, `setActivity()` appends a game index and a best score, taking the payload to **exactly 31 bytes**. There is no slack left: a longer device name or a third AD structure pushes the manufacturer block off the air, and `buildPayload()` logs and drops it rather than transmitting a half-advertisement. Sharing off removes both fields rather than zeroing them — "not transmitted" has to be structural to be worth claiming.

`decode()` is the exact inverse of the manufacturer block and is what `BleScanner` reads peers with. Do not write a second parser: a transmit copy and a receive copy of a wire format drift, and the symptom is two consoles that silently cannot see each other.

**Two consoles can play chess over this.** A chess move rides the same block, in the four bytes the score uses, gated on its own flag — a session, a ply, two squares and an ack, and nothing about the player. An invitation reuses the poke's wire shape and timer because it is the same kind of thing; a move does not, because a move is a state rather than an event and must stay on the air until it is replaced. `PAYLOAD_VERSION` is 4 for this: a move and a score are both thirteen bytes, so only the flag separates them and a version-3 reader would show a score of several million. That is the same failure that took version 2 to 3, which is why every field is now gated on its length **and** its flag. The full layout is in [docs/BLE_BEACON_SPEC.md](../../docs/BLE_BEACON_SPEC.md).

`startRadio()` and `stopRadio()` both take a `Watchdog::Pause` — bringing the controller up blocks for a couple of hundred milliseconds and looks exactly like a hang from the loop task. `pause()` no-ops while the watchdog is unarmed, so calling this from `Board::begin()` is safe.

Stopping is two separate things, and conflating them crashed the device. Advertising stops immediately and unconditionally -- that is the half the privacy switch means. The controller teardown (`NimBLEDevice::deinit(true)`) is **deferred while Wi-Fi is up**, and `tickRadio()`, called once per frame from the runtime, completes it once Wi-Fi goes down.

Measured on hardware: with Wi-Fi running for NTP, `advertising->stop()` followed by `deinit(true)` panics with `Core 0 panic'ed (InstrFetchProhibited), PC : 0x00000000` -- a call through a null function pointer, the controller being torn down while coexistence callbacks still reference it. It is a race, not a certainty; consecutive runs alternated between the panic and a survivable `timeout when WiFi un-init, type=4`. Steady-state coexistence is fine (five rounds of `scanNetworks()` against a live advertisement, free heap flat at ~155 KB), so this is deinit ordering and not memory -- and `Watchdog::Pause` does not help, because a panic is not a stall. The reachable gesture is switching the beacon off in Settings while the clock is syncing.

Deferring costs the ~30 KB of heap the stack holds, and only on a device that has Wi-Fi configured and the beacon off, and only until Wi-Fi next goes idle. "Off" still means off on the air, which is the part anyone can observe.

## BleScanner.{h,cpp}

Passive observer for other Braino beacons, in namespace `BleScan`. The radio half of Nearby play and nothing else — it holds no opinion about scores or notifications; that policy is `engine/NearbyPlay`, which is where the score catalog lives. The hal never has to know what a game is and the policy never has to know what an AD structure is.

Three properties are contract, not incidental:

- **Passive scan.** Never transmits a scan request, so listening adds nothing to what the device puts on air.
- **Both tests.** A peer must match the `Braino-` name prefix *and* decode as our manufacturer block at a known version.
- **Zero allocation.** Fixed eight-entry table; the advertisement is parsed straight out of the controller's buffer rather than through the library's `std::string` accessors, and `setMaxResults(0)` stops NimBLE keeping its own results vector. The callback runs on the NimBLE host task at whatever rate the air is busy — exactly the churn that fragments this heap.

The table is written from the host task and read from the loop task, so every touch of it sits inside a `portMUX` critical section, and `at()` returns **by value**. `startScan()`/`stopScan()` take a `Watchdog::Pause` for the same reason `BleBeacon` does.

## Watchdog.{h,cpp}

Background supervisor for the main loop, in namespace `Watchdog`. Games never interact with it.

Two independent layers: the ESP32 hardware task watchdog subscribed to the Arduino loop task (reboots after `TIMEOUT_SECONDS = 12`, so a hung game can't leave a player at a frozen screen), and a low-priority FreeRTOS monitor task pinned to core 0 that samples a heartbeat, frame times and heap once a second. The monitor logs a stall at `STALL_WARN_MS = 3000` — well before the hardware watchdog fires — and keeps a breadcrumb in RTC memory recording the active screen, uptime and heap low-water mark. On the next boot the breadcrumb is printed, and if the reset was unclean it's persisted to NVS so it survives a power cycle too. `lastRun()` exposes it.

Two contracts worth respecting:

- The hardware watchdog is armed lazily on the **first `feed()`**, not in `begin()`, because boot can legitimately block for minutes inside the touch-calibration wizard waiting for a finger.
- Anything that blocks for a long time on purpose must sit between `pause()` and `resume()` — calibration and factory reset already do.

Use `setContext()` to label the current screen so a post-crash report says where it happened. The runtime sets it on every screen change from the active app definition, or to `Launcher` for the home screen.

`pause()`/`resume()` are reference counted and `Watchdog::Pause` is the RAII form — prefer it, because several of these call sites have early returns. Three places in `Board` already use it: `runTouchCalibration()` (waits for a finger), `ntpUdpProbe()` (DNS plus a 4s reply wait) and `detectTimezone()` (DNS plus a 5s HTTP round trip). Add the guard to anything new that blocks the loop task for seconds.

The crash record uses its **own** NVS namespace, separate from the application data, and `factoryReset()` deliberately leaves it alone — a parent resetting a device that keeps crashing is exactly when that history matters. It is written only after an unclean reset, so flash wear is a non-issue; the per-second breadcrumb goes to RTC memory, which costs nothing.

`begin()` is called from `BrainoApp::begin()` right after `Board::begin()` (so the crash report is the first thing in the log) and `feed()` is the first statement in `BrainoApp::loop()`. The bringup env is wired the same way.

## ESP32 boards: hardware notes

*Moved here from the root `CLAUDE.md`, verbatim. They are facts about the
ESP32 boards' silicon and wiring; the Linux platform's are in
`host/CLAUDE.md`, and each board's own page is in `docs/boards/`.*

Pins live in `include/boards/<board>.h` -- one profile header per supported
board, and the only place in the tree that names a GPIO. Read the profile
rather than trusting generic ESP32 pinouts online, and never copy a pin map
between CYD variants: GPIO34 is battery sense here and the light sensor on the
ESP32-2432S028R. `docs/PORTING.md` is the checklist for adding a board.

- **A panel can be colour-inverted, and TFT_eSPI's macro will not fix it.**
  A CYD panel can invert every channel relative to the driver it is built
  with: the board comes up with backlight, touch, layout and a clean serial
  log, and only the colours are wrong, which reads as a theme bug. `TFT_INVERT_COLORS` is consulted **only** by TFT_eSPI's ST7789 and
  ST7735 init paths, so defining it on an ILI9341 build does nothing at all --
  that was tried on the bench and looked like the flag being ignored. The fix
  is `PanelProfile::invertColours`, which `Board::begin()` turns into a runtime
  `invertDisplay()` after `init()` and before anything is drawn.
- **The boot log states what the board IS.** `[boot] device=...` is the
  firmware's own id (`Board::deviceId()`), never the MAC -- see "No identifiers
  in this repository". `board=` and `panel=` are compiled in and therefore
  describe the firmware, not the hardware, which is exactly how a 2.8-inch
  board reported itself as a 4-inch for half an hour.
- **A running board answers `identify` with the same facts, unreset.** One
  line, every value quoted; `ESP32_boardUtil.py` asks before it resets
  anything. It carries **only the banner's facts** (no MAC, profile, score or
  SSID) and needs no PIN. Open the port with DTR and RTS already low, or
  opening it resets the board and defeats the point.
- **An EN reset can strand an E32R40T, and the flash tool now recovers it.**
  After the reset button, an RTS reset from a serial tool, a USB power surge
  -- and, most often, the hard reset at the end of an upload -- the ROM loops
  `flash read err, 988` or `invalid header: 0x20000368` / `RTCWDT_RTC_RESET`
  every ~350ms with the panel dark. **It is not the GPIO12 strap**, which was
  the first guess (GPIO12 is also the shared MISO): the ROM prints
  `boot:0x17`, whose MTDI bit is clear, so 3.3V flash was selected. **Nor is
  the image damaged**: read back while looping, the bootloader at 0x1000 was
  byte for byte correct, and the failing read differed from it by three bits
  at the same address -- a marginal read at the moment of some resets. Cause
  not yet found. What recovers it without a battery pull is a reset that comes
  *from download mode*: `esptool --after no_reset flash_id`, then `esptool
  --before no_reset --after hard_reset read_mac`, repeating the pair (never
  the second step alone) if it fails. `ESP32_boardUtil.py --flash` asks every
  board it flashed for its build afterwards and runs that recovery on any
  whose serial shows the loop (`check_boot()`), so a flash no longer leaves a
  dark 4-inch board behind. By hand: capture the serial passively before
  reflashing; it cannot be flashed while looping.
- **There is no `id=` field in the banner, and putting one back needs more
  care than it looks.** 5.9.0 printed the controller's ID register and broke the
  display on two of the seven boards: `tft.readcommand8()` writes an
  undocumented 0xD9, toggles CS mid-sequence and restores neither the address
  window nor MADCTL, so a panel whose MISO is not wired back is left
  mid-command and the next write inherits it. The symptom is the top bar
  crushed into a band at one edge and the rest of the screen never painted,
  while the loop reports 48fps, a 23ms worst frame and a flat heap -- every
  instrument saying the firmware is healthy, because it is. The corruption was
  in the panel, put there by the diagnostic. Across all seven boards the field
  only ever answered `00:00:00` or `FF:FF:FF`; not one returned a real ID, so
  it could not do the job it cost a panel to attempt. The remaining three lines
  turn "which board is this?" into a paste rather than an afternoon.
- **The E32R28T-1's RGB LED is red IO22, green IO16, blue IO17**, from the vendor's pin table, and **IO4 is its amplifier enable, active low** -- not an LED. It was once declared `rgb.r = 16`, `rgb.g = 4` on the belief that red and green were crossed; the observations behind that (orange came out green, purple came out cyan) are exactly what IO16 being green produces, and driving IO4 as an LED held the speaker's amplifier in shutdown. Common anode, so drive is inverted -- which the profile states rather than the driver assuming.
- Touch is bit-banged SPI on the E32R28T-1 and the ESP32-2432S028 variants
  (the TFT owns HSPI); on the E32R32P and the E32R40T the XPT2046 **shares the
  display bus** with its own CS on GPIO33, and `TOUCH_CS` in the board section
  is what switches `BoardTouch.cpp` to TFT_eSPI's touch extension. Either way,
  3-point affine calibration persisted in NVS behind a magic number. `touch.pressureThreshold = 350`, `touch.hitSlop = 8` in the profile.
- **A touch reset can be an expander port rather than a GPIO.** The Attaky
  Core wires its FT6636's reset to port P1_3 of an AW9523 at 0x59, so
  `TouchProfile` carries `resetExpanderAddress` and `resetExpanderPort` beside
  `reset`, and `Board::begin()` brings I2C up early to hold it through the
  panel init and release it after -- the same pulse a GPIO reset gets. It
  writes every AW9523 register it depends on rather than issuing a software
  reset, because that part wants a settle and a boot-time `delay()` is what
  `check_frame_rules.py` refuses here; and it leaves every other port an
  input, since the firmware reads none of them. That board was ported from
  Attaky's documents before a device existed here -- see
  `docs/boards/Attaky-Core.md` for what is still unchecked.
- **A battery can be a gauge on I2C rather than a divider.** The Attaky Core's
  Power module reports the cell on the touch bus, so `BatteryProfile` carries
  `fuelGaugeAddress` (0 on every other board) beside the ADC fields, and a
  board states one or the other, never both. `hasBatterySense()` covers
  either; `hasBatteryAdc()` is what guards the ADC pin. **Attaky make three
  power modules with three different chips**, and `FuelGaugeModule.cpp`
  identifies the one fitted the way Attaky's own Retro-Go does: a MAX17048 at
  0x36 by its VERSION register, an INA226 by its die ID, an INA219 by a config
  write it reads back -- never by a bare address ack, because the INA parts'
  addresses overlap and their voltage maths differs. The first module seen on
  a device was the INA219 one, not the MAX17048 the catalog documents; that
  is why. A MAX17048's state of charge goes to the percentage directly; an
  INA reports only the voltage (`Modules::PERCENT_FROM_VOLTS`) and goes
  through the LiPo curve -- **with its top at the module's own full
  voltage** (`Modules::gaugeFullVolts()`, `Board::curvePercentTo()`): the
  Standard Cell 1.1 reads 4.07 V with its full-charge light lit, which the
  curve's 4.20 V showed as 87%. Only the part above the curve's 70% point
  moves, so the low and critical warnings are where they were. Same task, same snapshot, same deadband and
  thresholds. **A gauge
  that does not answer is no reading** -- -1, no badge, no warning -- never an
  empty cell, because the module unclips. See `src/hal/CLAUDE.md`.
- Backlight brightness floors at `Board::BRIGHTNESS_MIN = 25` — at lower duty the panel is unreadable and a player could not see the slider to undo it.
- `audio.speakerPin = 26` on the E32R28T-1, the ESP32-2432S028 inverted-panel
  variant, E32R32P and E32R40T
  reaches the JST speaker connector via the ESP32 built-in DAC (DAC channel 2
  = GPIO26). `GUME_HAS_AUDIO_DAC 1` is set on all of them; the I2S
  peripheral drives the DAC directly via `I2S_DAC_BUILT_IN` with no external
  codec. The full cue vocabulary and the spoken boot phrase play from the same
  synthesiser as the Freenove FNK0104B. The codec path is `GUME_HAS_AUDIO_CODEC
  1` (FNK0104B only). The third is `GUME_HAS_AUDIO_I2S 1` (Attaky Core only):
  a bare I2S amplifier, the Media I/O module's NS4168, on BCLK IO17, word
  select IO21 and data IO18 with no codec, no MCLK and no enable line -- the
  codec path without the I2C, standard I2S at 16 kHz, volume as a linear
  multiplier in software like the DAC's. `GUME_HAS_AUDIO` (`BoardConfig.h`)
  is the one "has any audio path" test, every audio guard uses it, and a board
  stating two backends fails to compile; boards with none have no audio.
  `maxVolume` in `BoardProfile.audio` is 85 for the codec board and 75 for the
  built-in-DAC boards, which feed an onboard amplifier (the 1-inch driver
  distorts above 75%), and a provisional 50 on the Attaky Core, which nobody
  has heard yet -- it is to be set by ear on the device, as the others were.
  See `src/hal/CLAUDE.md`.
  Since 5.10.0 it is **on** for the E32R28T-1 and the inverted-panel CYD too,
  whose touch clock is GPIO25 -- DAC channel 1, the 5.5.0 collision.
  `beginAudio()` powers that channel down and returns the pad to the GPIO
  matrix, logging `[audio] GPIO25 released: ...`, and `Board::begin()`
  re-applies the touch pins after it; `BoardConfig.h` allows a touch pin on
  the non-speaker DAC pad and nothing else.
- Wi-Fi/NTP is a non-blocking state machine driven by `tickTimeSync()` each frame, with a raw-UDP `ntpUdpProbe()` fallback for when lwIP's SNTP never answers. The success-path automatic resync interval is a cached global setting, 1–24 hours with a 6-hour default; boot sync, manual sync and failure retries are separate. Timezone comes from a named POSIX zone or public-IP lookup — routers don't advertise one in practice.

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit -->

---

*Part of [Braino!](https://github.com/iamankushpandit/Gume) by [iamankushpandit](https://github.com/iamankushpandit). Copyright © 2026 iamankushpandit, licensed [GPL-3.0-or-later](https://github.com/iamankushpandit/Gume/blob/main/LICENSE) alongside the code — reuse of this document, in whole or in part, must keep this attribution and stay under the same licence. See [NOTICE.md](https://github.com/iamankushpandit/Gume/blob/main/NOTICE.md).*
