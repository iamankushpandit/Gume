#pragma once

#include <Arduino.h>

/* Background supervisor for the main loop.
 *
 * Two independent layers, neither of which any game has to know about:
 *
 *  1. The ESP32 hardware task watchdog, subscribed to the Arduino loop task.
 *     If a frame ever takes longer than TIMEOUT_SECONDS the chip panics and
 *     reboots, so a hung game can never leave a child staring at a frozen
 *     screen.
 *  2. A low priority FreeRTOS monitor task pinned to core 0 that samples a
 *     heartbeat counter, frame times and heap once a second. It logs a stall
 *     warning well before the hardware watchdog fires, and keeps a breadcrumb
 *     in RTC memory (which survives a reset) recording which screen was up,
 *     how long the device had been running and how bad the heap had got.
 *
 * After a crash the breadcrumb is read back on the next boot, printed to the
 * serial log and -- only when the reset was unclean -- written to NVS so it
 * also survives a power cycle. lastRun() exposes it for a diagnostics screen.
 *
 * The hardware watchdog is armed lazily on the first feed() rather than in
 * begin(), because boot can legitimately block for minutes inside the touch
 * calibration wizard while it waits for a finger. Anything else that blocks
 * for a long time on purpose should sit between pause() and resume(). */
namespace Watchdog {

/** Hardware reboot threshold. A frame this slow is a hang, not slow code. */
constexpr uint32_t TIMEOUT_SECONDS = 12;
/** Logged as a stall long before the hardware watchdog would reboot. */
constexpr uint32_t STALL_WARN_MS = 3000;
/** Free heap below this is logged as a warning every monitor tick. */
constexpr uint32_t HEAP_WARN_BYTES = 24 * 1024;
/* A screen may end up this far below where it started without comment --
 * lazily filled caches elsewhere are normal. Beyond it, say so. */
constexpr uint32_t SCREEN_LEAK_WARN_BYTES = 2048;
/* Free space this shattered is worth a warning even when the total looks
 * healthy: the next big allocation is the one that fails. */
constexpr uint8_t FRAGMENTATION_WARN_PCT = 60;

constexpr size_t CONTEXT_MAX = 24;

struct Stats {
    uint32_t loops = 0;          // frames since boot
    uint32_t lastFrameMs = 0;    // duration of the frame most recently fed
    uint32_t lastWorkMs = 0;     // frame time spent working before delay()
    uint32_t maxFrameMs = 0;     // worst frame since boot
    uint32_t maxWorkMs = 0;      // worst work slice since boot
    uint32_t stalls = 0;         // times the loop went quiet past STALL_WARN_MS
    uint32_t freeHeap = 0;
    uint32_t minFreeHeap = 0;    // low water mark since boot
    uint32_t largestBlock = 0;   // biggest allocatable block, i.e. fragmentation
    uint32_t uptimeSeconds = 0;
    uint32_t bootCount = 0;
    bool armed = false;          // hardware watchdog subscribed
};

/** Post-mortem of the run before this one. */
struct LastRun {
    bool valid = false;          // a breadcrumb was found
    bool unclean = false;        // panic / watchdog / brownout reset
    char context[CONTEXT_MAX] = {0};
    uint32_t uptimeSeconds = 0;
    uint32_t maxFrameMs = 0;
    uint32_t minFreeHeap = 0;
    int resetReason = 0;         // esp_reset_reason_t
};

/** Start the monitor task and read back the previous run's breadcrumb. */
void begin();
/** Call once per frame from loop(). Arms the hardware watchdog on first call. */
void feed();
/** Record active work time for the frame that is about to sleep. */
void recordFrameWork(uint32_t workMs);

/** Label the current screen so a crash report says where it happened. */
void setContext(const char* tag);
const char* context();

/** Bracket deliberately long blocking work (calibration, factory reset). */
void pause();
void resume();

/** Scoped pause(), so an early return cannot leave the watchdog disarmed. */
struct Pause {
    Pause() { pause(); }
    ~Pause() { resume(); }
    Pause(const Pause&) = delete;
    Pause& operator=(const Pause&) = delete;
};

/* Heap accounting for a screen that has just been left.
 *
 * `heapAtEntry` is free heap captured immediately before that screen's
 * begin(). Anything materially below it after end() means the screen kept
 * something, and on a device with no garbage collector and no way to compact
 * the heap, "kept something" repeated a few hundred times is a device that
 * stops allocating. Logged, not fatal -- a one-off dip can be a cache another
 * subsystem filled while the screen happened to be up.
 *
 * Also tracks the worst fragmentation seen, which is the failure free heap
 * alone will not show you: plenty free, no single block big enough. */
void noteScreenLeft(const char* screen, uint32_t heapAtEntry);

/** 0 = one contiguous free block, 100 = free space entirely shattered. */
uint8_t heapFragmentation();

Stats stats();
const LastRun& lastRun();
const char* resetReasonText(int reason);
/** Dump the previous run and the current state to the serial log. */
void printReport();

}   // namespace Watchdog
