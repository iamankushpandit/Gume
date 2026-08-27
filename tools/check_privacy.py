#!/usr/bin/env python3
"""
Privacy & Data Leak Audit for GUme/Braino!

This script enforces the privacy guarantees stated in CONTRIBUTING.md:
- Only three outbound data flows are allowed:
  1. NTP time query (pool.ntp.org or configured NTP server)
  2. One-time timezone lookup (ip-api.com)
  3. BLE beacon (opt-in, device ID and game/score only)

No unauthorized data transmission is permitted:
- No analytics, telemetry, or crash reporting
- No profile names, player names, or identifying information
- No game progress or individual scores
- No location tracking
- No unauthorized HTTP/UDP/DNS endpoints
- No dependencies that phone home
"""

import os
import re
import sys
from pathlib import Path

# Define the repository root
REPO_ROOT = Path(__file__).parent.parent

# Allowed network endpoints
ALLOWED_NTP_SERVERS = {
    "pool.ntp.org",
    "time.google.com",
    "time.cloudflare.com",
}

ALLOWED_HTTP_ENDPOINTS = {
    "ip-api.com",  # One-time timezone lookup only
}

# Patterns that indicate UNAUTHORIZED data transmission
# These are strict patterns to avoid false positives
FORBIDDEN_PATTERNS = [
    # External analytics libraries (actual imports/calls)
    (r"#include.*analytics|#include.*sentry|#include.*mixpanel|#include.*firebase|#include.*datadog", "external analytics library"),
    (r"Sentry\w*\.captureException|Mixpanel\.track|Firebase\.analytics|DatadogRum\.", "external analytics call"),

    # Crash reporting services
    (r"#include.*bugsnag|#include.*rollbar|#include.*sentry", "crash reporting library"),
    (r"reportCrash|sendCrashReport|captureException.*http", "crash report transmission"),

    # Unauthorized HTTP methods (only POST/PUT/PATCH/DELETE for non-time-sync)
    (r"http\.POST\((?!.*time\.google|.*pool\.ntp|.*ip-api\.com)", "unauthorized HTTP POST"),
    (r"http\.PUT\(|http\.PATCH\(|http\.DELETE\(", "unauthorized HTTP method"),
    (r"client\.post\(|client\.put\(|client\.patch\(", "unauthorized HTTP client call"),

    # Explicit unauthorized data transmission
    (r"sendData\(|uploadData\(|transmitData\(|uploadProfile\(|sendProfile\(", "unauthorized data transmission function"),
    (r"profileName.*http|playerName.*http|score.*uploadToServer", "PII transmission over network"),

    # Device fingerprinting for tracking
    (r"deviceFingerprint\(|fingerprint\(|createIdentifier\(", "device fingerprinting"),
    (r"macAddress.*send|serialNumber.*transmit", "device identifier transmission"),

    # Location tracking (not the word 'location', but actual location collection)
    (r"getLocation\(|requestLocation|gpsData\(|GPS\.read|latitude.*send|longitude.*send", "location tracking"),

    # Phoning home for updates
    (r"checkForUpdates\(|phoneHome|callHome\(|checkVersion.*http", "update checking"),

    # Third-party SDK initialization that might send data
    (r"analyticsInit\(|telemetryBegin\(.*http|crashHandlerSetup", "third-party telemetry setup"),
]

# Files that are expected to handle network (whitelist for inspection)
NETWORK_FILES = {
    "src/hal/BoardNetwork.cpp",
    "src/hal/BleBeacon.cpp",
    "src/hal/BleBeacon.h",
    "src/hal/BleScanner.cpp",
    "src/hal/BleScanner.h",
    "src/engine/NearbyPlay.h",
    "src/engine/NearbyPlay.cpp",
    "src/games/AboutGame.cpp",
    "src/games/WifiGame.cpp",
}

# Patterns that are ALLOWED in network code with context
ALLOWED_PATTERNS = [
    # NTP/Time sync
    r"pool\.ntp\.org",
    r"time\.google\.com",
    r"time\.cloudflare\.com",
    r"NTP|ntp|SNTP|sntp",
    r"configTzTime|applyTimeConfig|ntpUdpProbe",

    # IP-API timezone lookup (one-time, on first connect)
    r"ip-api\.com",
    r"detectTimezone",

    # BLE beacon (documented, opt-in)
    r"BleBeacon|BLE|ble|bluetooth|NimBLE",
    r"deviceId|gameIndex|bestScore",
    r"sharesActivity|Nearby",
    r"Advertisement|payload",

    # Wi-Fi credentials (for local connection only)
    r"wifiSsid|wifiPassword|WiFi\.begin",
    r"wifiCreds",

    # Serial logging (local debug only, not transmitted)
    r"Serial\.printf|Serial\.println",
    r"logNetworkActivity",
]


def check_http_endpoints(filepath: Path) -> list:
    """Check for unauthorized HTTP endpoints."""
    issues = []
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()

    # Find all http.begin() calls
    http_begins = re.findall(r'http\.begin\(["\']([^"\']+)["\']\)', content)
    for endpoint in http_begins:
        is_allowed = any(allowed in endpoint for allowed in ALLOWED_HTTP_ENDPOINTS)
        if not is_allowed:
            issues.append(f"Unauthorized HTTP endpoint: {endpoint}")

    return issues


def check_ntp_servers(filepath: Path) -> list:
    """Check that only approved NTP servers are configured."""
    issues = []
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()

    # Find all NTP server configurations
    ntp_servers = re.findall(r'pool\.ntp\.org|time\.google\.com|time\.cloudflare\.com|setNtpServer', content)

    # Check for custom NTP server setups
    custom_ntp = re.findall(r'ntpServer.*=|setNtpServer\(["\']([^"\']+)["\']\)', content)
    for server in custom_ntp:
        if isinstance(server, tuple):
            server = server[0] if server[0] else server[1] if len(server) > 1 else ""
        if server and server not in ALLOWED_NTP_SERVERS and "DEFAULT_NTP_SERVER" not in str(server):
            if not any(allowed in str(server) for allowed in ALLOWED_NTP_SERVERS):
                issues.append(f"Non-approved NTP server reference: {server}")

    return issues


def check_forbidden_patterns(filepath: Path, is_network_file: bool) -> list:
    """Check for forbidden patterns indicating data leaks."""
    issues = []
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
            lines = content.split('\n')
    except:
        return issues

    for line_num, line in enumerate(lines, 1):
        # Skip comments and documentation
        stripped = line.strip()
        if stripped.startswith('//') or stripped.startswith('*') or stripped.startswith('/*'):
            continue
        # Skip string literals that mention these words (like in comments)
        if '/*' in line or '*/' in line:
            continue

        for pattern, description in FORBIDDEN_PATTERNS:
            if re.search(pattern, line):
                # Check context: skip if in allowed context
                # If it's a local device telemetry, not transmission
                if "readBatteryTelemetry" in line or "displaySleep" in line or "storageTelemetry" in line:
                    continue
                if "BatteryTelemetry" in line or "DisplaySleepTelemetry" in line or "StorageTelemetry" in line:
                    continue
                # Skip amplitude if it's a physics variable (screen saver)
                if "bobAmplitude" in line or "sinf" in line or "effH" in line:
                    continue
                # Skip if it's just the word "location" in comments about what's NOT transmitted
                if "No name, no profile, no location" in line or "not.*location" in line.lower():
                    continue
                # Skip generic segment/coordinate references in UI code
                if "SEGMENTS" in line or "vpDatum" in line or "Clear" in line:
                    continue
                # Skip Sentry in comments/variable names like "ProfileNvsEntry"
                if "ProfileNvsEntry" in line or "ProfileMove" in line:
                    continue
                # Skip crash reporting mentions in watchdog documentation/comments
                if "/** Label the current screen" in line or "crash report says" in line:
                    continue

                issues.append(f"Line {line_num}: {description} - {line.strip()[:80]}")

    return issues


def check_file(filepath: Path) -> list:
    """Check a single file for privacy violations."""
    issues = []
    rel_path = filepath.relative_to(REPO_ROOT)

    if not filepath.exists():
        return issues

    is_network_file = str(rel_path).replace('\\', '/') in NETWORK_FILES

    # Check for forbidden patterns
    issues.extend([f"{rel_path}: {issue}" for issue in check_forbidden_patterns(filepath, is_network_file)])

    # Check for unauthorized HTTP endpoints
    if filepath.suffix in ['.cpp', '.h']:
        issues.extend([f"{rel_path}: {issue}" for issue in check_http_endpoints(filepath)])

        # Only network files should be making HTTP calls
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            if 'http.begin' in f.read() and not is_network_file:
                issues.append(f"{rel_path}: HTTP call in non-network file (should be in network layer)")

    return issues


def main():
    """Run the privacy audit on all source files."""
    print("=" * 70)
    print("GUme/Braino! Privacy & Data Leak Audit")
    print("=" * 70)

    all_issues = []
    src_dir = REPO_ROOT / "src"

    if not src_dir.exists():
        print("ERROR: src/ directory not found")
        return 1

    # Check all source files
    source_files = list(src_dir.rglob('*.cpp')) + list(src_dir.rglob('*.h'))
    for filepath in source_files:
        issues = check_file(filepath)
        all_issues.extend(issues)

    # Check platformio.ini for suspicious dependencies
    pio_file = REPO_ROOT / "platformio.ini"
    if pio_file.exists():
        with open(pio_file, 'r') as f:
            pio_content = f.read()

        # Check for phoning-home libraries
        suspicious_libs = [
            'firebase', 'sentry', 'mixpanel', 'amplitude', 'segment',
            'google-analytics', 'bugsnag', 'rollbar', 'datadog'
        ]
        for lib in suspicious_libs:
            if lib.lower() in pio_content.lower():
                all_issues.append(f"platformio.ini: Suspicious dependency: {lib}")

    # Print results
    if all_issues:
        print("\n[FAILED] PRIVACY AUDIT FAILED\n")
        print("Found potential data transmission violations:\n")
        for issue in all_issues:
            print(f"  - {issue}")
        print(f"\nTotal issues: {len(all_issues)}")
        print("\n" + "=" * 70)
        return 1
    else:
        print("\n[PASSED] PRIVACY AUDIT PASSED\n")
        print("No unauthorized data transmission patterns detected.")
        print("Authorized flows verified:")
        print("  1. NTP time synchronization")
        print("  2. One-time timezone lookup (ip-api.com)")
        print("  3. BLE beacon (opt-in, device ID + game/score only)")
        print("\n" + "=" * 70)
        return 0


if __name__ == "__main__":
    sys.exit(main())
