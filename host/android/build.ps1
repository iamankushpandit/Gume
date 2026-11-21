# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

# Builds Braino! for Android and packs it into build/android/braino.apk:
#
#   powershell -File host/android/build.ps1            # build the APK
#   powershell -File host/android/build.ps1 -Install   # and install it over ADB
#
# Needs the Android SDK with the NDK, CMake and build-tools (host/android/
# CLAUDE.md says which), and a JDK for apksigner. Nothing here is Gradle:
# the APK is a manifest and one native library, so the SDK's own tools pack
# it directly -- aapt2 for the manifest, zip for the library, zipalign and
# apksigner to finish.
#
# The APK is signed with a per-machine debug key kept outside the tree
# ($env:LOCALAPPDATA\braino\android-debug.keystore). It is never committed:
# a signing key in a public repository signs for anyone. Keeping the same key
# across builds is what lets `adb install -r` replace the installed Braino
# with its profiles and scores intact.

param(
    [switch]$Install,
    [string]$Sdk = $(if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { "$env:LOCALAPPDATA\Android\Sdk" }),
    [string]$Ndk = "29.0.14206865",
    [string]$CMake = "3.31.6",
    [string]$BuildTools = "36.1.0",
    [string]$Platform = "android-36"
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo "build\android"
$cmakeBin = Join-Path $Sdk "cmake\$CMake\bin"
$ndkDir = Join-Path $Sdk "ndk\$Ndk"
$bt = Join-Path $Sdk "build-tools\$BuildTools"
$androidJar = Join-Path $Sdk "platforms\$Platform\android.jar"
foreach ($need in @($cmakeBin, $ndkDir, $bt, $androidJar)) {
    if (-not (Test-Path $need)) { throw "missing $need -- see host/android/CLAUDE.md" }
}
$env:PATH = "$cmakeBin;$env:PATH"

function Invoke-Step([string]$what, [scriptblock]$cmd) {
    Write-Host "[android] $what"
    # The tools report progress on stderr, which Windows PowerShell turns into
    # a terminating error under "Stop". Success is the exit code.
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try { & $cmd } finally { $ErrorActionPreference = $saved }
    if ($LASTEXITCODE -ne 0) { throw "$what failed ($LASTEXITCODE)" }
}

Invoke-Step "configure" {
    & "$cmakeBin\cmake.exe" -S (Join-Path $repo "host\android") -B $build -G Ninja `
        "-DCMAKE_MAKE_PROGRAM=$cmakeBin\ninja.exe" `
        "-DCMAKE_TOOLCHAIN_FILE=$ndkDir\build\cmake\android.toolchain.cmake" `
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DCMAKE_BUILD_TYPE=Release | Out-Null
}
Invoke-Step "build libbraino.so" { & "$cmakeBin\cmake.exe" --build $build }

$lib = Join-Path $build "libbraino.so"
$stage = Join-Path $build "apk"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $stage "lib\arm64-v8a") | Out-Null
Copy-Item $lib (Join-Path $stage "lib\arm64-v8a\libbraino.so")
# The NDK's own strip: the debug symbols stay in build/android/libbraino.so.
& (Join-Path $ndkDir "toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe") `
    --strip-unneeded (Join-Path $stage "lib\arm64-v8a\libbraino.so")

$unsigned = Join-Path $build "braino-unsigned.apk"
$aligned = Join-Path $build "braino-aligned.apk"
$apk = Join-Path $build "braino.apk"
Invoke-Step "pack the manifest" {
    & (Join-Path $bt "aapt2.exe") link -o $unsigned -I $androidJar `
        --manifest (Join-Path $PSScriptRoot "AndroidManifest.xml") `
        --min-sdk-version 28 --target-sdk-version 28
}
Invoke-Step "add the library" {
    Push-Location $stage
    try { & jar uf $unsigned "lib/arm64-v8a/libbraino.so" } finally { Pop-Location }
}
Invoke-Step "zipalign" { & (Join-Path $bt "zipalign.exe") -f -p 4 $unsigned $aligned }

$keyDir = Join-Path $env:LOCALAPPDATA "braino"
$ks = Join-Path $keyDir "android-debug.keystore"
if (-not (Test-Path $ks)) {
    New-Item -ItemType Directory -Force $keyDir | Out-Null
    Invoke-Step "make this machine's debug key" {
        & keytool -genkeypair -keystore $ks -storepass android -keypass android `
            -alias braino -keyalg RSA -keysize 2048 -validity 10000 `
            -dname "CN=Braino debug, O=Braino" | Out-Null
    }
}
Invoke-Step "sign" {
    & (Join-Path $bt "apksigner.bat") sign --ks $ks --ks-pass pass:android `
        --key-pass pass:android --out $apk $aligned
}
"[android] built {0} ({1:N1} MB)" -f $apk, ((Get-Item $apk).Length / 1MB)

if ($Install) {
    Invoke-Step "install" { & adb install -r $apk }
    # Android 9 shows an app Wi-Fi names and scan results only behind the
    # location permission, which an app is otherwise asked for in a dialog
    # Braino has no Java to show. Braino never asks where the device is; see
    # HostWifiOsAndroid.cpp.
    Invoke-Step "grant the Wi-Fi names permission" {
        & adb shell pm grant io.github.iamankushpandit.braino android.permission.ACCESS_FINE_LOCATION
    }
}
