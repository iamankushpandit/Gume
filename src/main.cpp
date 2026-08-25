#include <Arduino.h>
#include "engine/AppRuntime.h"
#include "hal/Board.h"
#include "hal/Clock.h"
#include "hal/Watchdog.h"
#include "ui/TftRenderer.h"
#include "ui/Ui.h"

#ifdef CYD_BRINGUP_ONLY

Board board;
bool redraw = true;

void drawBringup(const TouchPoint& touch = TouchPoint{}) {
    Ui::TftRenderer tft(board.display());
    Ui::clear(tft);
    tft.fillRect(0, 0, SCREEN_WIDTH, TOP_BAR_HEIGHT, Ui::surface());
    tft.setTextColor(TFT_WHITE, Ui::surface());
    tft.setTextDatum(ML_DATUM);
    tft.drawString("Hello Board", 10, TOP_BAR_HEIGHT / 2, 4);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(Clock::timeText(), SCREEN_WIDTH - 6, TOP_BAR_HEIGHT / 2, 2);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString("Display: OK", 18, 46, 4);
    tft.drawString(String("SD card: ") + (board.sdReady() ? "mounted" : "not found"), 18, 78, 4);
    tft.drawString(String("Touch: ") + (board.hasTouchCalibration() ? "calibrated" : "needs calibration"), 18, 110, 4);
    tft.drawString(String("Last touch: ") + touch.x + "," + touch.y, 18, 142, 2);
    Ui::drawButton(tft, Rect{28, 178, 124, 42}, "Calibrate", Ui::rgb(36, 132, 204), TFT_DARKGREY, TFT_WHITE);
    Ui::drawButton(tft, Rect{168, 178, 124, 42}, "SD Retry", Ui::rgb(45, 154, 96), TFT_DARKGREY, TFT_WHITE);
}

void setup() {
    board.begin();
    Watchdog::begin();
    Watchdog::setContext("bringup");
    Clock::begin();
    if (!board.hasTouchCalibration()) {
        board.runTouchCalibration();
    }
    drawBringup();
}

void loop() {
    Watchdog::feed();
    const TouchPoint touch = board.pollTouch();
    if (touch.justPressed) {
        if (Rect{28, 178, 124, 42}.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            board.runTouchCalibration();
            redraw = true;
        } else if (Rect{168, 178, 124, 42}.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            board.mountSd();
            redraw = true;
        }
    }
    if (touch.justPressed || touch.justReleased) {
        redraw = true;
    }
    if (redraw) {
        drawBringup(touch);
        redraw = false;
    }
    delay(20);
}

#else

BrainoApp app;

void setup() {
    app.begin();
}

void loop() {
    app.loop();
}

#endif
