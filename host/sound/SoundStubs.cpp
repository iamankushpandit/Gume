// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* What the synthesiser needs from the rest of the firmware, answered with
 * nothing.
 *
 * braino-sounds compiles src/hal/AudioSynth.cpp, BoardAudio.cpp and
 * BoardAudioCues.cpp and nothing else from src/, so everything those two files reach outside
 * themselves has to be stood in for here. None of it may make a sound or
 * change one: the point of the program is that every sample comes from the
 * firmware's own code. So the console is never muted, the amplifier is a
 * flag, the generator task is never started -- SoundRender.cpp pulls samples
 * with audio::generateBlock(), as the task would -- and the I2S driver is
 * never reached at all.
 *
 * If this file grows a definition that computes anything, it has stopped
 * being a stand-in. */

#include "hal/BoardAudioInternal.h"

#include <driver/i2s.h>

#include <cstdio>

/* The real constructor is in Board.cpp, with the whole of bring-up behind it.
 * Every member the synthesiser reads -- the sound and volume mirrors -- is
 * initialised where it is declared, so an empty body leaves them exactly as a
 * board has them before anything is loaded. */
Board::Board() {}

void Board::pulseRgb(uint8_t, uint8_t, uint8_t, uint16_t) {}

/* Never muted, and nothing is stored: every setting reads as its default. */
bool Preferences::getBool(const char*, bool fallback) { return fallback; }
size_t Preferences::putBool(const char*, bool) { return 1; }

/* Serial goes to stderr, so the boot log lines the synthesiser prints cannot
 * end up inside a table on stdout. */
HardwareSerial Serial;
size_t Print::write(const uint8_t* data, size_t len) {
    return std::fwrite(data, 1, len, stderr);
}
size_t Print::print(const char* s) { return std::fputs(s, stderr) >= 0 ? 1 : 0; }
size_t Print::println() { return std::fputc('\n', stderr) == '\n' ? 1 : 0; }
int HardwareSerial::available() { return 0; }
int HardwareSerial::read() { return -1; }
int HardwareSerial::peek() { return -1; }
size_t HardwareSerial::write(uint8_t c) { return std::fputc(c, stderr) == c ? 1 : 0; }
size_t HardwareSerial::write(const uint8_t* data, size_t len) {
    return std::fwrite(data, 1, len, stderr);
}
void HardwareSerial::flush() { std::fflush(stderr); }

uint32_t millis() { return 0; }

/* One thread, no task: a lock nobody else wants, and a task that is refused. */
SemaphoreHandle_t xSemaphoreCreateMutex() { return nullptr; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t,
                                   TaskHandle_t*, BaseType_t) {
    return pdFAIL;
}
void xTaskNotifyGive(TaskHandle_t) {}
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t) { return 0; }
esp_err_t i2s_write(i2s_port_t, const void*, size_t size, size_t* written, TickType_t) {
    *written = size;
    return ESP_OK;
}

namespace audio {
void setAmp(bool on) { ampOn = on; }
void tickAmpTail() {}
#if GUME_HAS_AUDIO_DAC
int dacIdleLeft = 0;
void fillDacIdle(TickType_t) { dacIdleLeft = 0; }
#endif
}   // namespace audio
