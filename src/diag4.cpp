/* Standalone bring-up probe for the 4-inch ST7796 CYD board.
 *
 * ------------------------------------------------------------------------
 * Why this is a separate firmware
 * ------------------------------------------------------------------------
 * Every fact this board needs stated in a profile header is currently a
 * guess. The abandoned feat/st7796-4inch-board branch says so in its own
 * comments: the display SPI pins are an "unverified guess that this is the
 * same reference PCB family with a bigger panel bolted on", and the backlight
 * is "GPIO21 produced no backlight at all -- trying 27 next".
 *
 * Those guesses cannot be checked from inside the app, because all three ways
 * of being wrong produce the same symptom: a dark screen with a healthy
 * serial log. Wrong SPI bus, wrong driver and wrong backlight pin are
 * indistinguishable by eye. So this builds ALONE
 * (build_src_filter = +<diag4.cpp>, env:diag4), exactly as wifi_diag.cpp does
 * for the radio and battery_diag.cpp for the ADC.
 *
 * Two build decisions here are the whole point of the tool:
 *
 *   - TFT_BL is deliberately NOT defined. If TFT_eSPI owned the backlight it
 *     would drive one pin we already suspect, and page BL could not sweep the
 *     alternatives. The panel is lit from this file, as plain GPIO, or not at
 *     all.
 *   - GUME_BOARD_HEADER is deliberately NOT defined, so this does not include
 *     BoardConfig.h. There is no profile for this board yet -- producing the
 *     numbers that go in one is what this firmware is for. It must not depend
 *     on the answer it exists to find.
 *
 * ------------------------------------------------------------------------
 * The one test that separates the failure modes
 * ------------------------------------------------------------------------
 * Page ID reads the controller's identification registers back over MISO.
 * That answers "are the SPI pins right?" and "which controller is this?" in
 * one shot, without the backlight working and without anything being visible.
 * If it reports an ID, the bus is correct and any remaining blankness is the
 * backlight. If it reports nothing but 0x00 or 0xFF, either the bus is wrong
 * or MISO is not wired back on this variant.
 *
 * This matters more than it sounds: ST7796S accepts most ILI9341 commands, so
 * a mismatched driver renders a real but mirrored, partly-garbled image rather
 * than nothing -- which is easy to read as "sort of working" and chase for an
 * afternoon. The ID register does not have opinions.
 *
 * ------------------------------------------------------------------------
 * Using it
 * ------------------------------------------------------------------------
 *   pio run -e diag4 -t upload && pio device monitor
 *
 * BOOT (IO0) cycles pages. Over serial: 'n' next page, 'p' previous, 'r'
 * re-run the current page, '0'-'6' jump to a page directly.
 *
 * Everything reports on serial. Nothing here requires the screen to work,
 * which is the point -- report what you see, including "still dark".
 */

#include <Arduino.h>
#include <TFT_eSPI.h>

// ---------------------------------------------------------------- candidates

namespace cfg {

/* Backlight candidates, in the order worth trying. 21 is the E32R28T-1's pin
 * and the first thing anyone assumes; 27 is the branch's untested guess and a
 * common alternate on ST7796 CYD variants; the rest are pins these boards are
 * known to route a backlight to on some revision or other.
 *
 * GPIO6-11 are the SPI flash and are excluded on pain of a brick. GPIO34-39
 * are input-only and cannot drive anything. The display bus (12/13/14/15/2)
 * is excluded because driving it would fight the driver under test. */
constexpr uint8_t BACKLIGHT_CANDIDATES[] = {21, 27, 32, 5, 16, 17, 4, 22};

/* Touch on the 2.8-inch board is a separate bit-banged bus. The branch claims
 * the 4-inch board shares the display's physical SPI pins instead, arbitrated
 * by its own CS. Page TOUCH-B tests the 2.8-inch wiring so the two can be
 * told apart by measurement on this unit rather than by inference from a
 * manufacturer PDF for a similarly-named board. */
constexpr uint8_t BB_TOUCH_MOSI = 32;
constexpr uint8_t BB_TOUCH_MISO = 39;
constexpr uint8_t BB_TOUCH_SCLK = 25;
constexpr uint8_t BB_TOUCH_CS = 33;
constexpr uint8_t BB_TOUCH_IRQ = 36;

/* Long enough to look up from the serial log at the panel, short enough that
 * a full sweep is not a coffee break. */
constexpr uint32_t BL_DWELL_MS = 2500;
constexpr uint32_t ROT_DWELL_MS = 3000;

constexpr uint8_t PIN_BOOT = 0;

}  // namespace cfg

// ---------------------------------------------------------------- state

TFT_eSPI tft;

enum Page : uint8_t {
    PAGE_ID = 0,
    PAGE_BL,
    PAGE_PATTERN,
    PAGE_ROT,
    PAGE_TOUCH_SHARED,
    PAGE_TOUCH_BITBANG,
    PAGE_PINS,
    PAGE_COUNT
};

const char* const PAGE_NAMES[PAGE_COUNT] = {
    "ID       -- who is on the SPI bus, if anyone",
    "BL       -- sweep every plausible backlight pin",
    "PATTERN  -- geometry, colour order and mirroring",
    "ROT      -- which rotation is landscape with USB at the bottom",
    "TOUCH-S  -- touch sharing the display SPI bus",
    "TOUCH-B  -- touch on its own bit-banged bus (2.8-inch wiring)",
    "PINS     -- ADC on candidate battery-sense pins",
};

uint8_t page = PAGE_ID;

// ---------------------------------------------------------------- helpers

void banner(const char* name) {
    Serial.println();
    Serial.println("========================================================");
    Serial.printf("PAGE %u: %s\n", page, name);
    Serial.println("========================================================");
}

// ---------------------------------------------------------------- pages

/* PAGE ID -- the only page that does not care whether anything is visible.
 *
 * 0x04 (RDDID) and 0xD3 (RDDIDIF) are the two identification reads these
 * controllers answer. A working bus gives a stable, non-trivial pattern:
 * ST7796 answers 0xD3 with 0x77 0x96, ILI9341 with 0x93 0x41. All-0x00 or
 * all-0xFF means MISO is idle -- nothing is talking back. */
void runId() {
    Serial.printf("Driver compiled in : %s\n",
#if defined(ST7796_DRIVER)
                  "ST7796_DRIVER"
#elif defined(ILI9341_2_DRIVER)
                  "ILI9341_2_DRIVER"
#else
                  "(unknown)"
#endif
    );
    Serial.printf("SPI pins           : MISO=%d MOSI=%d SCLK=%d CS=%d DC=%d RST=%d\n",
                  TFT_MISO, TFT_MOSI, TFT_SCLK, TFT_CS, TFT_DC, TFT_RST);
    Serial.printf("Declared geometry  : TFT_WIDTH=%d TFT_HEIGHT=%d\n", TFT_WIDTH, TFT_HEIGHT);
    Serial.printf("Reported by driver : width=%d height=%d (rotation %d)\n",
                  tft.width(), tft.height(), tft.getRotation());

    uint8_t d3[4];
    uint8_t d04[4];
    for (uint8_t i = 0; i < 4; ++i) {
        d3[i] = tft.readcommand8(0xD3, i);
    }
    for (uint8_t i = 0; i < 4; ++i) {
        d04[i] = tft.readcommand8(0x04, i);
    }

    Serial.printf("RDDIDIF (0xD3)     : %02X %02X %02X %02X\n", d3[0], d3[1], d3[2], d3[3]);
    Serial.printf("RDDID   (0x04)     : %02X %02X %02X %02X\n", d04[0], d04[1], d04[2], d04[3]);

    /* Idle means every payload byte is a rail: all 0x00 or all 0xFF, in any
     * mixture across the two commands. An earlier version of this required
     * both commands to agree on WHICH rail, and so read 0xD3 = 00 00 00 00
     * with 0x04 = FF FF FF FF as "something answered". Nothing answered. A
     * floating MISO does not have to float to the same rail twice. */
    bool idle = true;
    for (uint8_t i = 1; i < 4 && idle; ++i) {
        if (d3[i] != 0x00 && d3[i] != 0xFF) {
            idle = false;
        }
        if (d04[i] != 0x00 && d04[i] != 0xFF) {
            idle = false;
        }
    }

    if (idle) {
        Serial.println("VERDICT: MISO is idle -- nothing answered.");
        Serial.println("         This is INCONCLUSIVE on this board family, not a failure.");
        Serial.println("         The 2.8in E32R28T-1 drives an identical bus (MISO=12 MOSI=13");
        Serial.println("         SCLK=14 CS=15 DC=2, HSPI) and works, and CYD boards commonly");
        Serial.println("         do not wire the panel's MISO back to the MCU at all -- there");
        Serial.println("         is nothing to read from even when every write lands.");
        Serial.println("         So this does NOT tell you the pins are wrong. Page PATTERN");
        Serial.println("         decides it: a correct image means the write path is right and");
        Serial.println("         only the read path is missing, which costs this firmware");
        Serial.println("         nothing -- Braino never reads the panel back.");
    } else if (d3[1] == 0x77 && d3[2] == 0x96) {
        Serial.println("VERDICT: ST7796 confirmed. Bus and driver are both right.");
    } else if (d3[1] == 0x93 && d3[2] == 0x41) {
        Serial.println("VERDICT: ILI9341, NOT ST7796. This panel is a different controller");
        Serial.println("         than assumed -- change the driver flag before anything else.");
    } else {
        Serial.println("VERDICT: something answered, but it is neither ST7796 (77 96) nor");
        Serial.println("         ILI9341 (93 41). Report these bytes -- the controller needs");
        Serial.println("         identifying before a profile can be written.");
    }
}

/* PAGE BL -- the sweep. Both polarities per pin, because the backlight
 * transistor is driven active-low on some of these boards and active-high on
 * others, and guessing the polarity wrong looks exactly like a wrong pin. */
void runBacklightSweep() {
    Serial.println("Filling the panel white first, so a lit backlight is unmistakable.");
    Serial.println("Watch the screen. Report the pin AND polarity that lit it.");
    tft.fillScreen(TFT_WHITE);

    for (uint8_t pin : cfg::BACKLIGHT_CANDIDATES) {
        pinMode(pin, OUTPUT);
        Serial.printf("  GPIO%-2u  HIGH ... ", pin);
        Serial.flush();
        digitalWrite(pin, HIGH);
        delay(cfg::BL_DWELL_MS);
        Serial.print("LOW ... ");
        Serial.flush();
        digitalWrite(pin, LOW);
        delay(cfg::BL_DWELL_MS);
        Serial.println("next");
        // Released, so the next candidate is tested on its own.
        pinMode(pin, INPUT);
    }
    Serial.println("Sweep complete. If NOTHING lit the panel at any point, the backlight is");
    Serial.println("not on any of these pins -- or the panel is not being driven at all,");
    Serial.println("which page ID would already have told you.");
    Serial.println("Once you know the pin, say so: pages PATTERN and ROT need the light on.");
}

/* PAGE PATTERN -- built so a WRONG result is diagnosable, not merely visible.
 *
 * Corner labels catch mirroring and 180-degree flips; named colour blocks
 * catch a BGR/RGB swap; the border catches an off-by-one geometry or an
 * addressable area smaller than the panel. A garbled-but-present image is the
 * signature of ST7796S accepting ILI9341 commands. */
void runPattern() {
    Serial.printf("Drawing into %dx%d at rotation %d.\n",
                  tft.width(), tft.height(), tft.getRotation());
    Serial.println("Report: are all four corner labels present and the right way round?");
    Serial.println("        is RED actually red (a BGR swap would show it blue)?");
    Serial.println("        does the white border touch all four physical edges?");

    const int16_t w = tft.width();
    const int16_t h = tft.height();

    tft.fillScreen(TFT_BLACK);
    tft.drawRect(0, 0, w, h, TFT_WHITE);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("TL", 6, 6, 4);
    tft.setTextDatum(TR_DATUM);
    tft.drawString("TR", w - 6, 6, 4);
    tft.setTextDatum(BL_DATUM);
    tft.drawString("BL", 6, h - 6, 4);
    tft.setTextDatum(BR_DATUM);
    tft.drawString("BR", w - 6, h - 6, 4);

    /* Named blocks: the label says what the colour should be, so a swap is
     * self-evident from a photograph with nothing to compare against. */
    struct Swatch {
        uint16_t colour;
        const char* name;
    };
    const Swatch swatches[] = {
        {TFT_RED, "RED"},
        {TFT_GREEN, "GREEN"},
        {TFT_BLUE, "BLUE"},
        {TFT_YELLOW, "YELLOW"},
    };
    const int16_t bw = w / 4;
    const int16_t by = h / 2 - 30;
    tft.setTextDatum(MC_DATUM);
    for (uint8_t i = 0; i < 4; ++i) {
        tft.fillRect(i * bw, by, bw, 60, swatches[i].colour);
        tft.setTextColor(TFT_BLACK, swatches[i].colour);
        tft.drawString(swatches[i].name, i * bw + bw / 2, by + 30, 2);
    }

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    char buf[48];
    snprintf(buf, sizeof(buf), "%dx%d rot%d", w, h, tft.getRotation());
    tft.drawString(buf, w / 2, by - 40, 4);
}

/* PAGE ROT -- resolves landscapeRotation, which is a physical fact about
 * where the USB socket is and cannot be derived from anything else. */
void runRotationSweep() {
    Serial.println("Cycling rotations 0-3. Report the number that reads upright with the");
    Serial.println("USB socket at the BOTTOM -- that is this board's landscapeRotation.");
    const uint8_t entry = tft.getRotation();
    for (uint8_t r = 0; r < 4; ++r) {
        tft.setRotation(r);
        tft.fillScreen(TFT_NAVY);
        tft.setTextColor(TFT_WHITE, TFT_NAVY);
        tft.setTextDatum(MC_DATUM);
        char buf[24];
        snprintf(buf, sizeof(buf), "ROT %u", r);
        tft.drawString(buf, tft.width() / 2, tft.height() / 2 - 20, 4);
        snprintf(buf, sizeof(buf), "%dx%d", tft.width(), tft.height());
        tft.drawString(buf, tft.width() / 2, tft.height() / 2 + 20, 2);
        // Should point at the USB edge when r is the right one.
        tft.drawString("vvv  USB HERE  vvv", tft.width() / 2, tft.height() - 24, 2);
        Serial.printf("  rotation %u -> %dx%d\n", r, tft.width(), tft.height());
        delay(cfg::ROT_DWELL_MS);
    }
    tft.setRotation(entry);
    Serial.printf("Restored rotation %u.\n", entry);
}

/* PAGE TOUCH-S -- the branch's claim, tested. TFT_eSPI's built-in touch
 * extension drives the XPT2046 over the display's own SPI bus, arbitrated by
 * TOUCH_CS. Sane values that MOVE with the press are the confirmation; a
 * frozen number or a constant 0/4095 is not. */
void runTouchShared() {
#if defined(TOUCH_CS)
    Serial.printf("XPT2046 over the shared display bus, CS=%d. Press the panel.\n", TOUCH_CS);
    Serial.println("Looking for raw x/y that change with WHERE you press. 10 seconds.");
    const uint32_t until = millis() + 10000;
    uint16_t samples = 0;
    while (millis() < until) {
        uint16_t rx = 0;
        uint16_t ry = 0;
        if (tft.getTouchRaw(&rx, &ry)) {
            Serial.printf("  raw x=%4u y=%4u z=%4u\n", rx, ry, tft.getTouchRawZ());
            ++samples;
            delay(120);
        }
        delay(10);
    }
    Serial.printf("%u sample(s) in 10s.\n", samples);
    if (samples == 0) {
        Serial.println("VERDICT: nothing. Either touch is NOT on the shared bus on this unit,");
        Serial.println("         or TOUCH_CS is wrong. Try page TOUCH-B.");
    } else {
        Serial.println("VERDICT: the shared-bus wiring responds. Confirm the numbers tracked");
        Serial.println("         your finger rather than sitting still.");
    }
#else
    Serial.println("Not built with TOUCH_CS -- nothing to test on this page.");
#endif
}

/* Minimal XPT2046 exchange on plain GPIO, matching how the 2.8-inch board is
 * driven. Kept here rather than shared with the HAL on purpose: this file must
 * not depend on the board abstraction it exists to inform. */
uint16_t bbTransfer(uint8_t command) {
    digitalWrite(cfg::BB_TOUCH_CS, LOW);
    for (int8_t i = 7; i >= 0; --i) {
        digitalWrite(cfg::BB_TOUCH_MOSI, (command >> i) & 1);
        digitalWrite(cfg::BB_TOUCH_SCLK, HIGH);
        delayMicroseconds(1);
        digitalWrite(cfg::BB_TOUCH_SCLK, LOW);
        delayMicroseconds(1);
    }
    uint16_t value = 0;
    for (uint8_t i = 0; i < 16; ++i) {
        digitalWrite(cfg::BB_TOUCH_SCLK, HIGH);
        delayMicroseconds(1);
        value = static_cast<uint16_t>((value << 1) | digitalRead(cfg::BB_TOUCH_MISO));
        digitalWrite(cfg::BB_TOUCH_SCLK, LOW);
        delayMicroseconds(1);
    }
    digitalWrite(cfg::BB_TOUCH_CS, HIGH);
    return static_cast<uint16_t>(value >> 4);  // 12-bit result, left-aligned in 16.
}

/* PAGE TOUCH-B -- the 2.8-inch board's wiring: a separate bit-banged bus.
 * Between this page and TOUCH-S, "shared" versus "separate" becomes a
 * measurement on this unit instead of an assumption. */
void runTouchBitbang() {
    Serial.printf("XPT2046 on its own bus: MOSI=%u MISO=%u SCLK=%u CS=%u IRQ=%u\n",
                  cfg::BB_TOUCH_MOSI, cfg::BB_TOUCH_MISO, cfg::BB_TOUCH_SCLK,
                  cfg::BB_TOUCH_CS, cfg::BB_TOUCH_IRQ);
    Serial.println("Press the panel. 10 seconds.");

    pinMode(cfg::BB_TOUCH_MOSI, OUTPUT);
    pinMode(cfg::BB_TOUCH_SCLK, OUTPUT);
    pinMode(cfg::BB_TOUCH_CS, OUTPUT);
    pinMode(cfg::BB_TOUCH_MISO, INPUT);
    pinMode(cfg::BB_TOUCH_IRQ, INPUT);
    digitalWrite(cfg::BB_TOUCH_CS, HIGH);
    digitalWrite(cfg::BB_TOUCH_SCLK, LOW);

    const uint32_t until = millis() + 10000;
    uint16_t hits = 0;
    while (millis() < until) {
        const uint16_t z1 = bbTransfer(0xB1);
        const uint16_t x = bbTransfer(0x91);
        const uint16_t y = bbTransfer(0xD1);
        if (z1 > 200 && z1 < 4000) {
            Serial.printf("  raw x=%4u y=%4u z=%4u irq=%d\n",
                          x, y, z1, digitalRead(cfg::BB_TOUCH_IRQ));
            ++hits;
            delay(120);
        }
        delay(10);
    }
    Serial.printf("%u sample(s) in 10s.\n", hits);
    if (hits == 0) {
        Serial.println("VERDICT: nothing on the separate bus. If TOUCH-S produced numbers,");
        Serial.println("         this board shares the display bus, as the branch claimed.");
    } else {
        Serial.println("VERDICT: the separate bus responds -- this board is wired like the");
        Serial.println("         2.8-inch one, and the shared-bus claim is wrong here.");
    }
}

/* PAGE PINS -- battery sense is optional hardware, but a profile has to state
 * either a pin or PIN_NONE, and guessing wrong ships a fictional percentage.
 * ADC1 only: ADC2 is unusable whenever Wi-Fi is associated. */
void runPinScan() {
    Serial.println("ADC1 inputs, averaged. On the E32R28T-1 battery sense is IO34 behind a");
    Serial.println("2:1 divider, so a charged cell reads about 1900-2100 mV at the pin.");
    Serial.println("Report which pin MOVES when you unplug the battery.");
    const uint8_t adc1[] = {32, 33, 34, 35, 36, 39};
    analogReadResolution(12);
    for (uint8_t pin : adc1) {
        uint32_t sum = 0;
        for (uint8_t i = 0; i < 32; ++i) {
            sum += analogRead(pin);
            delayMicroseconds(200);
        }
        const uint32_t raw = sum / 32;
        Serial.printf("  GPIO%-2u  raw=%4u  ~%4u mV at the pin  (~%4u mV before a 2:1 divider)\n",
                      pin, static_cast<unsigned>(raw),
                      static_cast<unsigned>((raw * 3300) / 4095),
                      static_cast<unsigned>((raw * 3300 * 2) / 4095));
    }
    Serial.println("NOTE: 32 and 33 double as touch lines on some variants -- a reading there");
    Serial.println("      may be this firmware's own output rather than a sensor.");
}

void runPage() {
    banner(PAGE_NAMES[page]);
    switch (page) {
        case PAGE_ID: runId(); break;
        case PAGE_BL: runBacklightSweep(); break;
        case PAGE_PATTERN: runPattern(); break;
        case PAGE_ROT: runRotationSweep(); break;
        case PAGE_TOUCH_SHARED: runTouchShared(); break;
        case PAGE_TOUCH_BITBANG: runTouchBitbang(); break;
        case PAGE_PINS: runPinScan(); break;
        default: break;
    }
    Serial.println("-- BOOT or 'n' for the next page, 'p' back, 'r' repeat, 0-6 to jump.");
}

// ---------------------------------------------------------------- lifecycle

void setup() {
    Serial.begin(115200);
    delay(600);
    Serial.println();
    Serial.println("Braino 4-inch bring-up probe (env:diag4)");
    Serial.println("Nothing here assumes the screen works. Report what you SEE, including");
    Serial.println("'still dark' -- that is a result, not a failure to observe one.");

    pinMode(cfg::PIN_BOOT, INPUT_PULLUP);

    tft.init();
    tft.setRotation(3);

    /* Hold the two most likely backlight pins up front, so the panel has a
     * chance of being visible before the sweep is reached. Page BL releases
     * and tests them properly. */
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);
    pinMode(27, OUTPUT);
    digitalWrite(27, HIGH);

    runPage();
}

void loop() {
    static bool bootWasDown = false;
    const bool bootDown = digitalRead(cfg::PIN_BOOT) == LOW;
    bool advance = false;

    if (bootDown && !bootWasDown) {
        advance = true;
    }
    bootWasDown = bootDown;

    if (Serial.available()) {
        const int c = Serial.read();
        if (c == 'n') {
            advance = true;
        } else if (c == 'p') {
            page = static_cast<uint8_t>((page + PAGE_COUNT - 1) % PAGE_COUNT);
            runPage();
        } else if (c == 'r') {
            runPage();
        } else if (c >= '0' && c < '0' + PAGE_COUNT) {
            page = static_cast<uint8_t>(c - '0');
            runPage();
        }
    }

    if (advance) {
        page = static_cast<uint8_t>((page + 1) % PAGE_COUNT);
        runPage();
    }

    delay(20);
}
