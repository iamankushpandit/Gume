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

## Input

The first finger down is the FT6336U's one contact, followed until it lifts.
Android's Back key is the BOOT key, so it goes Home; it is consumed, so the
system never finishes the activity on it.

## Radios and the network

- **Bluetooth: unavailable, and says so.** `HostNimBLE.cpp` opens a raw HCI
  socket, which Android refuses an app. The beacon and Nearby report no
  controller rather than pretending.
- **Wi-Fi is the system's.** The Portal joins networks in its own setup;
  `HostWifiOs.cpp` finds neither NetworkManager nor wpa_supplicant here.
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

## Signing

`build.ps1` signs with a per-machine debug key at
`$env:LOCALAPPDATA\braino\android-debug.keystore`, made on first use and
**never committed** -- a key in a public repository signs for anyone. The same
key across builds is what lets `adb install -r` keep the profiles and scores.
