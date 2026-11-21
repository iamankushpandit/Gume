# host/android/

Braino! on Android, starting with the **Meta Portal+** (first generation,
codename `aloha`: Snapdragon 835-class, 15.6-inch 1920x1080, Android 9).
`src/` is compiled **unchanged**, exactly as `host/` compiles it for a
desktop and for the RG40XX V, into `libbraino.so`. The system's own
`android.app.NativeActivity` loads it. **There is no Java and no dex in the
package** (`android:hasCode="false"`): an APK here is a manifest and one
native library.

```powershell
powershell -File host/android/build.ps1            # build/android/braino.apk
powershell -File host/android/build.ps1 -Install   # and adb install -r it
adb logcat -s braino                               # every [host]/[boot] line
```

Needs the Android SDK with `ndk;29.0.14206865`, `cmake;3.31.6`,
`build-tools;36.1.0` and `platforms;android-36`, and a JDK on `PATH` for
`jar`, `keytool` and `apksigner`. No Gradle, no Android Studio project.

## Why an APK, and why it is still the whole device

The Portal+'s bootloader is locked with verified boot enforced (green); it
boots only Meta-signed images, and its download mode is Meta's own protocol
(USB `2EC6`), not fastboot. A whole-device image the way `rg40xxv/` builds one
cannot be booted on it. What Meta does allow is ADB, so Braino goes on as an
APK -- and the manifest makes that APK the device's **HOME** activity: what it
boots into and what every way back leads to. The packaging is an APK; the
player sees a Braino, not an app on someone else's home screen.

## What this build replaces

`CMakeLists.txt` includes `../cmake/BrainoSources.cmake`, the same source list,
pinned libraries and include roots as every host build, and swaps exactly
these from `host/src/`:

| `host/src/` | Here | Why |
|---|---|---|
| `HostPlatform.cpp` (SDL window, mouse, gamepad) | `src/HostPlatformAndroid.cpp` | The panel is the activity's window; the touch controller is the touchscreen |
| `HostI2s.cpp` (SDL audio queue) | `src/HostI2sAAudio.cpp` | AAudio, with the same ~96 ms of buffer as the codec board's DMA ring |
| `HostMain.cpp`'s `main()` | `src/AndroidMain.cpp`'s `android_main()` | A NativeActivity has no `main()`; it calls the same `hostMain()` |
| `HostDrm.cpp` | not built | The window is the panel here |
| `HostWifiOs.cpp` (NetworkManager / wpa_supplicant) | `src/HostWifiOsAndroid.cpp` | Android owns the radio: `WifiManager` over JNI |
| `HostDevicePose.cpp` (no sensor, or `BRAINO_POSE`) | `src/HostDevicePoseAndroid.cpp` | The accelerometer says which way up the Portal is held |

Everything else in `host/src/` and `host/shim/` is the desktop's own file.

## The panel

`HostPlatformAndroid.cpp` repeats `HostPlatform.cpp`'s `findPanel()`: the
largest whole UI scale that still leaves the 320x240 game canvas. On the
Portal+ that is **4**, so the firmware lays out on **480x270** -- the launcher
and the system screens use all of it, and a game is its canvas centred, as on
every panel larger than the canvas.

The window is given the frame's own size and format (`RGB_565`), so a present
is a row copy into the buffer `ANativeWindow_lock()` hands back -- the same
shape as `HostDrm`'s back buffer. An unchanged frame is not presented again.
The emulated backlight dims the picture, because the window has no lamp of its
own to turn down. The activity is locked to landscape: every game is authored
against the landscape canvas.

## Auto-rotate

The Portal+'s stand pivots the screen, and it has an accelerometer (a
BMA250E). The activity stays in landscape -- the window never turns, like a
board's panel -- and `HostDevicePoseAndroid.cpp` reports which way up the
device is held through `src/hal/DevicePose.h`. With Auto chosen in Settings,
which is the default wherever there is a sensor, the firmware's layout and
half-turn follow that pose: the launcher and the system screens stand up in
portrait, games stay landscape, and everything turns over with a half turn.

The sensor's queue is attached to `android_main()`'s looper, so its events
are read in `HostAndroid::pump()` on the firmware's own thread. A pose counts
once one edge has been lowest for half a second; flat on a table, the last
pose stands. Each change is logged with the gravity that decided it --
`[pose] portrait (gravity x=-9.6 y=0.4)` -- which is how to check that a
quarter turn one way is Portrait and the other Portrait 180.

## The sandbox kills, it does not refuse

Android runs every app under a seccomp filter, and a system call it forbids
is answered with SIGSYS: the process dies. Braino is the home activity, so
Android restarts it at once into the same call -- a loop of boot greetings.
Two calls on that list are made here: `settimeofday()`, by the firmware's NTP
sync once a saved network lets it run, and `mount()`, by `HostSleep.cpp` when
the panel sleeps. `src/HostSandboxAndroid.cpp` answers both as a desktop
answers a program that is not root -- -1 and `EPERM` -- through
`-Wl,--wrap=` in `CMakeLists.txt`, so neither reaches the kernel. Android
keeps the Portal's clock synced itself, and that is the clock the firmware
reads.

**A crash loop on the device is the first thing to look for here.**
`adb logcat -b crash` names the call -- "seccomp prevented call to
disallowed arm64 system call N" -- and the fix goes beside these two.

## Input

The first finger down is the FT6336U's one contact, followed until it lifts.
Android's Back key is the BOOT key, so it goes Home; it is consumed, so the
system never finishes the activity on it.

## Radios and the network

- **Bluetooth: unavailable, and says so.** `HostNimBLE.cpp` opens a raw HCI
  socket, which Android refuses an app. The beacon and Nearby report no
  controller rather than pretending.
- **Wi-Fi is Android's, asked through `WifiManager`.**
  `src/HostWifiOsAndroid.cpp` replaces `HostWifiOs.cpp` and answers the same
  contract over JNI: the link (name, signal, address, gateway) refreshed once
  a second on its own thread, a scan when the Wi-Fi app asks, and a join of
  the network the owner types there. Nothing is originated: Android already
  holds the link, and traffic still leaves only by the firmware's own flows.
- **The location permission, and why.** Android 9 shows an app the name of
  the network it is on, and what a scan found, only if it holds
  `ACCESS_FINE_LOCATION` and location is on. Braino never asks where the
  device is; the permission is what Android puts in front of Wi-Fi names.
  An app normally asks for it in a dialog, and Braino has no Java to show
  one, so `build.ps1 -Install` grants it (`adb shell pm grant`). Without it
  the link still works and the clock still syncs; the network's name reads
  empty and a scan finds nothing.
- **HTTP through libcurl, built here, HTTP only.** Android ships no libcurl,
  so `CMakeLists.txt` builds a pinned one with every optional feature off.
  `HostNet.cpp` is unchanged. The time zone lookup is plain HTTP and works;
  the update check is HTTPS and fails cleanly. Giving it TLS means choosing a
  library to trust with it, which is its own change.

The outbound flows are the firmware's, as on every host build: nothing in this
directory originates a request.

## Data

`HostPlatform::dataDir()` is the app's private storage
(`/data/data/io.github.iamankushpandit.braino/files/braino`): `nvs.txt`,
`radio-id` and the rest. No other app can read it, and it is removed with
Braino.

**The sealing key is there too, and that is deliberate.** `HostSecret` seals
the Wi-Fi network and the admin PIN with a key from the processor, because a
handheld's `nvs.txt` sits on a card any laptop reads. An Android app can read
no processor serial -- the Portal+ exposes none of `HostSecret`'s sources, and
SELinux is enforcing -- so this build defines `GUME_HOST_SANDBOXED_KEY` and the
key is 32 random bytes made once in `secret-key`, beside `nvs.txt`. The
package is not debuggable and keeps no backup, so not even `adb` reads either
file; a copy of `nvs.txt` that leaves without the key opens nothing. The log
line `[secrets] sealing with a key from the app's private storage` says which
it was.

## Making the Portal Braino's alone

`build.ps1 -Install` installs Braino; it does not take the device over. That
is three `adb` steps, done once per device, in this order -- Braino first, so
the Portal is never left without a home screen:

```powershell
adb shell cmd package set-home-activity "io.github.iamankushpandit.braino/android.app.NativeActivity"
```

Then each of Meta's user-facing apps, and the stock ones Braino does not need,
removed for the device's user (`pm uninstall --user 0`): the package stays in
the system image, so nothing is destroyed.

```powershell
foreach ($p in "com.facebook.alohaapps.launcher","com.facebook.alohaapps.controlcenter","com.facebook.alohaapps.contacts","com.facebook.aloha.app.messenger","com.facebook.aloha.app.whatsapp","com.facebook.aloha.app.portalfeed","com.facebook.aloha.app.storytime","com.facebook.alohaapps.superframe","com.facebook.aloha.app.cameraeditor","com.facebook.portal.aiservice","org.chromium.chrome","com.android.camera2") { adb shell pm uninstall --user 0 $p }
```

Left alone on purpose: Meta's system services and device setup, which Android
needs to boot; Meta's Settings app, which holds the ADB switch and is
unreachable once its home screen and control panel are gone; and Android's
own `com.android.settings/.FallbackHome`, which shows for a moment at boot.

Any of them comes back with `adb shell cmd package install-existing <package>`,
and a factory reset restores the lot.

The boot logo is Meta's and stays Meta's: the first is the bootloader's
`splash` partition, the animation is `/system/media/bootanimation.zip` on the
verified, read-only system partition, and the boot animation reads no
writable path (`/oem` is read-only too). Braino's own splash follows it.

## Signing

`build.ps1` signs with a per-machine debug key at
`$env:LOCALAPPDATA\braino\android-debug.keystore`, made on first use and
**never committed** -- a key in a public repository signs for anyone. The same
key across builds is what lets `adb install -r` keep the profiles and scores.

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit -->

---

*Part of [Braino!](https://github.com/iamankushpandit/Gume) by [iamankushpandit](https://github.com/iamankushpandit). Copyright © 2026 iamankushpandit, licensed [GPL-3.0-or-later](https://github.com/iamankushpandit/Gume/blob/main/LICENSE) alongside the code — reuse of this document, in whole or in part, must keep this attribution and stay under the same licence. See [NOTICE.md](https://github.com/iamankushpandit/Gume/blob/main/NOTICE.md).*
