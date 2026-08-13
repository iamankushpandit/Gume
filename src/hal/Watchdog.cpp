#include "Watchdog.h"

#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace Watchdog {
namespace {

constexpr uint32_t BREADCRUMB_MAGIC = 0x57445431UL;   // "WDT1"
constexpr uint32_t MONITOR_PERIOD_MS = 1000;
constexpr uint32_t HEAP_LOG_PERIOD_MS = 30000;
constexpr const char* NVS_NAMESPACE = "cydwdt";

/* Lives in RTC slow memory and is deliberately NOT zeroed by the startup code,
 * so it survives a panic, a task-watchdog reboot and a soft reset. Only a
 * power cycle clears it -- which is why an unclean run is also copied to NVS
 * on the following boot. Updating it costs nothing, so the monitor task
 * refreshes it every tick. */
struct Breadcrumb {
    uint32_t magic;
    uint32_t bootCount;
    uint32_t uptimeSeconds;
    uint32_t maxFrameMs;
    uint32_t minFreeHeap;
    char context[CONTEXT_MAX];
};

RTC_NOINIT_ATTR Breadcrumb rtcCrumb;

TaskHandle_t loopTask_ = nullptr;
TaskHandle_t monitorTask_ = nullptr;

volatile uint32_t heartbeat_ = 0;
volatile uint32_t lastFeedMs_ = 0;
volatile uint32_t lastFrameMs_ = 0;
volatile uint32_t lastWorkMs_ = 0;
volatile uint32_t maxFrameMs_ = 0;
volatile uint32_t maxWorkMs_ = 0;
volatile uint32_t stalls_ = 0;
volatile uint32_t minFreeHeap_ = 0xFFFFFFFFUL;
volatile bool armed_ = false;
volatile bool paused_ = false;
volatile uint8_t pauseDepth_ = 0;   // pause() nests: DNS inside a probe, etc.
volatile bool started_ = false;

char context_[CONTEXT_MAX] = "boot";
LastRun lastRun_;

void copyContext(char* dst, const char* src) {
    if (src == nullptr) {
        src = "?";
    }
    strncpy(dst, src, CONTEXT_MAX - 1);
    dst[CONTEXT_MAX - 1] = '\0';
}

bool uncleanReset(int reason) {
    switch (reason) {
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
        case ESP_RST_BROWNOUT:
            return true;
        default:
            return false;
    }
}

void updateCrumb() {
    rtcCrumb.magic = BREADCRUMB_MAGIC;
    rtcCrumb.uptimeSeconds = millis() / 1000UL;
    rtcCrumb.maxFrameMs = maxFrameMs_;
    rtcCrumb.minFreeHeap = minFreeHeap_ == 0xFFFFFFFFUL ? 0 : minFreeHeap_;
    copyContext(rtcCrumb.context, context_);
}

/* Read the previous run out of RTC memory, or -- if this was a cold boot and
 * RTC memory is gone -- out of NVS, where the last unclean run was parked. */
void recoverLastRun() {
    const int reason = static_cast<int>(esp_reset_reason());
    const bool rtcValid = rtcCrumb.magic == BREADCRUMB_MAGIC;

    if (rtcValid) {
        lastRun_.valid = true;
        lastRun_.uptimeSeconds = rtcCrumb.uptimeSeconds;
        lastRun_.maxFrameMs = rtcCrumb.maxFrameMs;
        lastRun_.minFreeHeap = rtcCrumb.minFreeHeap;
        copyContext(lastRun_.context, rtcCrumb.context);
        rtcCrumb.bootCount++;
    } else {
        rtcCrumb.bootCount = 1;
        rtcCrumb.maxFrameMs = 0;
        rtcCrumb.minFreeHeap = 0;
        copyContext(rtcCrumb.context, "boot");
        rtcCrumb.magic = BREADCRUMB_MAGIC;
    }
    lastRun_.resetReason = reason;
    lastRun_.unclean = uncleanReset(reason);

    Preferences prefs;
    if (lastRun_.unclean && lastRun_.valid) {
        // Rare by construction, so the flash wear does not matter.
        if (prefs.begin(NVS_NAMESPACE, false)) {
            prefs.putString("ctx", lastRun_.context);
            prefs.putUInt("up", lastRun_.uptimeSeconds);
            prefs.putUInt("frame", lastRun_.maxFrameMs);
            prefs.putUInt("heap", lastRun_.minFreeHeap);
            prefs.putInt("reason", lastRun_.resetReason);
            prefs.putUInt("boots", rtcCrumb.bootCount);
            prefs.end();
        }
    } else if (!lastRun_.valid) {
        // Cold boot: RTC memory is gone, but NVS may still hold the crash that
        // caused the power cycle in the first place.
        if (prefs.begin(NVS_NAMESPACE, true)) {
            const int stored = prefs.getInt("reason", 0);
            if (stored != 0) {
                lastRun_.valid = true;
                lastRun_.unclean = uncleanReset(stored);
                lastRun_.resetReason = stored;
                lastRun_.uptimeSeconds = prefs.getUInt("up", 0);
                lastRun_.maxFrameMs = prefs.getUInt("frame", 0);
                lastRun_.minFreeHeap = prefs.getUInt("heap", 0);
                copyContext(lastRun_.context, prefs.getString("ctx", "?").c_str());
            }
            const uint32_t boots = prefs.getUInt("boots", 0);
            prefs.end();
            if (boots > 0) {
                rtcCrumb.bootCount = boots + 1;
            }
        }
    }

    /* Persist the boot counter on every boot, not only after a crash.
     *
     * It used to be written inside the unclean-reset branch alone, so a cold
     * boot -- which is every power cycle, since RTC memory does not survive
     * one -- read back whatever the last crash had left and reported the same
     * number forever. The device sat on "boot #4" across many power cycles,
     * which quietly undermines the crash report it appears alongside. */
    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.putUInt("boots", rtcCrumb.bootCount);
        prefs.end();
    }
}

void monitorTask(void*) {
    uint32_t lastBeat = heartbeat_;
    uint32_t lastHeapLogMs = 0;
    bool stallLogged = false;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(MONITOR_PERIOD_MS));

        const uint32_t nowMs = millis();
        const uint32_t freeHeap = ESP.getFreeHeap();
        if (freeHeap < minFreeHeap_) {
            minFreeHeap_ = freeHeap;
        }

        const uint32_t beat = heartbeat_;
        const uint32_t sinceFeed = nowMs - lastFeedMs_;
        if (armed_ && !paused_ && beat == lastBeat && sinceFeed >= STALL_WARN_MS) {
            if (!stallLogged) {
                stalls_++;
                stallLogged = true;
                Serial.printf("[wdt] STALL %lums in '%s' (frames=%lu heap=%lu), "
                              "reboot at %lums\n",
                              (unsigned long)sinceFeed, context_,
                              (unsigned long)beat, (unsigned long)freeHeap,
                              (unsigned long)(TIMEOUT_SECONDS * 1000UL));
            }
        } else if (beat != lastBeat) {
            if (stallLogged) {
                Serial.printf("[wdt] recovered in '%s' after %lu stall(s)\n",
                              context_, (unsigned long)stalls_);
            }
            stallLogged = false;
        }
        lastBeat = beat;

        if (freeHeap < HEAP_WARN_BYTES) {
            Serial.printf("[wdt] LOW HEAP %lu bytes (min %lu, largest block %lu) in '%s'\n",
                          (unsigned long)freeHeap, (unsigned long)minFreeHeap_,
                          (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                          context_);
        } else if (nowMs - lastHeapLogMs >= HEAP_LOG_PERIOD_MS) {
            lastHeapLogMs = nowMs;
            Serial.printf("[wdt] up=%lus frames=%lu worst=%lums heap=%lu min=%lu ctx='%s'\n",
                          (unsigned long)(nowMs / 1000UL), (unsigned long)beat,
                          (unsigned long)maxFrameMs_, (unsigned long)freeHeap,
                          (unsigned long)minFreeHeap_, context_);
        }

        updateCrumb();
    }
}

void arm() {
    if (armed_) {
        return;
    }
    loopTask_ = xTaskGetCurrentTaskHandle();
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    const esp_task_wdt_config_t cfg = {
        .timeout_ms = TIMEOUT_SECONDS * 1000U,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    // The Arduino core has already initialised the TWDT; retune it to ours.
    if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) {
        esp_task_wdt_init(&cfg);
    }
#else
    esp_task_wdt_init(TIMEOUT_SECONDS, true);
#endif
    if (esp_task_wdt_add(loopTask_) == ESP_OK) {
        armed_ = true;
        Serial.printf("[wdt] armed: loop task, %lus timeout\n",
                      (unsigned long)TIMEOUT_SECONDS);
    } else {
        Serial.println("[wdt] could not subscribe the loop task");
    }
}

}   // namespace

void begin() {
    if (started_) {
        return;
    }
    started_ = true;

    recoverLastRun();
    minFreeHeap_ = ESP.getFreeHeap();
    lastFeedMs_ = millis();
    updateCrumb();
    printReport();

    // Priority 1 (just above idle) on core 0, away from the Arduino loop task
    // on core 1, so a busy frame cannot starve the monitor.
    xTaskCreatePinnedToCore(monitorTask, "wdtmon", 3072, nullptr, 1, &monitorTask_, 0);
}

void feed() {
    const uint32_t nowMs = millis();
    const uint32_t frameMs = nowMs - lastFeedMs_;
    lastFeedMs_ = nowMs;
    heartbeat_++;
    if (heartbeat_ > 1 && frameMs > maxFrameMs_) {
        maxFrameMs_ = frameMs;
    }
    lastFrameMs_ = frameMs;

    if (!armed_) {
        // Deferred until the first frame: boot can sit in the touch
        // calibration wizard indefinitely, waiting for a finger.
        arm();
        return;
    }
    if (!paused_) {
        esp_task_wdt_reset();
    }
}

void recordFrameWork(uint32_t workMs) {
    lastWorkMs_ = workMs;
    if (workMs > maxWorkMs_) {
        maxWorkMs_ = workMs;
    }
}

void setContext(const char* tag) {
    copyContext(context_, tag);
    copyContext(rtcCrumb.context, tag);
}

const char* context() {
    return context_;
}

void pause() {
    if (pauseDepth_ < 0xFF) {
        pauseDepth_++;
    }
    if (!armed_ || paused_) {
        return;
    }
    paused_ = true;
    esp_task_wdt_delete(loopTask_);
}

void resume() {
    if (pauseDepth_ > 0) {
        pauseDepth_--;
    }
    // Only the outermost resume() re-arms, so a nested guard cannot expose the
    // rest of a long blocking call to the watchdog.
    if (pauseDepth_ > 0 || !armed_ || !paused_) {
        return;
    }
    esp_task_wdt_add(loopTask_);
    esp_task_wdt_reset();
    lastFeedMs_ = millis();
    paused_ = false;
}

uint8_t heapFragmentation() {
    const uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap == 0) {
        return 100;
    }
    const uint32_t largest = ESP.getMaxAllocHeap();
    if (largest >= freeHeap) {
        return 0;
    }
    /* The standard measure: how much of what is free cannot be handed out in
     * one piece. Free heap on its own hides this completely. */
    return static_cast<uint8_t>(100UL - (largest * 100UL) / freeHeap);
}

void noteScreenLeft(const char* screen, uint32_t heapAtEntry) {
    if (heapAtEntry == 0) {
        return;   // no baseline captured, nothing to compare against
    }
    const uint32_t now = ESP.getFreeHeap();
    if (heapAtEntry > now && (heapAtEntry - now) >= SCREEN_LEAK_WARN_BYTES) {
        Serial.printf("[heap] '%s' left %lu bytes short (%lu -> %lu)\n",
                      screen == nullptr ? "?" : screen,
                      static_cast<unsigned long>(heapAtEntry - now),
                      static_cast<unsigned long>(heapAtEntry),
                      static_cast<unsigned long>(now));
    }
    const uint8_t frag = heapFragmentation();
    if (frag >= FRAGMENTATION_WARN_PCT) {
        Serial.printf("[heap] fragmentation %u%% after '%s' (free %lu, largest %lu)\n",
                      frag, screen == nullptr ? "?" : screen,
                      static_cast<unsigned long>(now),
                      static_cast<unsigned long>(ESP.getMaxAllocHeap()));
    }
}

Stats stats() {
    Stats s;
    s.loops = heartbeat_;
    s.lastFrameMs = lastFrameMs_;
    s.lastWorkMs = lastWorkMs_;
    s.maxFrameMs = maxFrameMs_;
    s.maxWorkMs = maxWorkMs_;
    s.stalls = stalls_;
    s.freeHeap = ESP.getFreeHeap();
    s.minFreeHeap = minFreeHeap_ == 0xFFFFFFFFUL ? s.freeHeap : minFreeHeap_;
    s.largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    s.uptimeSeconds = millis() / 1000UL;
    s.bootCount = rtcCrumb.bootCount;
    s.armed = armed_;
    return s;
}

const LastRun& lastRun() {
    return lastRun_;
}

const char* resetReasonText(int reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "power on";
        case ESP_RST_EXT:       return "external reset";
        case ESP_RST_SW:        return "software restart";
        case ESP_RST_PANIC:     return "panic / exception";
        case ESP_RST_INT_WDT:   return "interrupt watchdog";
        case ESP_RST_TASK_WDT:  return "task watchdog";
        case ESP_RST_WDT:       return "other watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep wake";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "sdio";
        default:                return "unknown";
    }
}

void printReport() {
    Serial.printf("[wdt] boot #%lu, reset: %s\n",
                  (unsigned long)rtcCrumb.bootCount,
                  resetReasonText(static_cast<int>(esp_reset_reason())));
    if (lastRun_.valid) {
        Serial.printf("[wdt] previous run: ctx='%s' up=%lus worst frame=%lums min heap=%lu%s\n",
                      lastRun_.context, (unsigned long)lastRun_.uptimeSeconds,
                      (unsigned long)lastRun_.maxFrameMs,
                      (unsigned long)lastRun_.minFreeHeap,
                      lastRun_.unclean ? "  <-- CRASHED" : "");
    }
}

}   // namespace Watchdog
