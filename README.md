# GoodTime Kids Educational Game Platform

Firmware for the Sunton ESP32-2432S028R "Cheap Yellow Display" with a small educational game engine. The dark themed launcher, seventeen games, and an About screen are compiled into flash; Memory and Counting can load settings from the microSD card.

## What Is Included

- `bringup` firmware: display, touch calibration, SD retry, and last-touch test screen.
- `app` firmware: launcher plus two-player Tic-Tac-Toe, 4x6 Memory Match, adaptive Math, Multiplication Tables, Time, Whack A Mole, Simon Says, Sudoku, Shape & Color, Counting, Money, Fraction Circles, progressive Maze, Sorting, Color Mix, Sliding Puzzle, Odd One Out, and About.
- SD content loader for `/games/memory/default.json` and `/games/counting/default.json`.
- Persistent score tracking where it makes sense: Tic-Tac-Toe match counts, Memory best moves, Math best correct-count/time, Multiplication Tables best streak, Time best streak, Whack A Mole best score, Simon best sequence, Sudoku best board size, Shape best taps, Counting best streak, Money best streak, Fraction Circles best streak, Maze best moves per level, Sorting best streak, Color Mix best streak, Sliding Puzzle best moves, and Odd One Out best streak.
- 12-hour AM/PM clock in the top-right corner. It starts from the firmware build time and advances with uptime; there is no RTC/NTP setup yet.
- Example SD card files under `data/sd`.
- A content schema in `docs/SD_CONTENT_SPEC.md`.

## Hardware Assumptions

- Board: ESP32-2432S028R, 2.8 inch ILI9341 TFT, 320 x 240 landscape UI.
- Touch: XPT2046 resistive touch on separate GPIOs. The app uses a bit-banged reader so touch does not occupy the SD card's VSPI bus.
- SD card: FAT32 microSD. The card is optional; Memory and Counting use it for editable settings.
- Audio: disabled in this firmware. There is no sound control in the UI.

## Windows Setup

1. Install VS Code.
2. Install the PlatformIO IDE extension in VS Code.
3. If Windows does not see the board as a COM port, install a CH340 USB serial driver.
4. Open this folder in VS Code: `C:\Users\Ankus\Documents\PlatformIO\Projects\GUme`.
5. Plug in the CYD with a data-capable USB cable.

PlatformIO CLI commands in PowerShell use:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e bringup -t upload
```

## First Flash: Bring-Up

Flash the hardware test firmware first:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e bringup -t upload
```

On first boot it asks for touch calibration. Tap and hold the three targets. After calibration, the screen shows display, SD, touch, and last-touch status. Use `SD Retry` after inserting a card.

## Flash The Full App

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e app -t upload
```

If touch has never been calibrated, the full app runs the same three-point calibration before opening the launcher.

## Prepare The SD Card

1. Format the microSD card as FAT32.
2. Copy the contents of `data/sd` to the root of the card.
3. The card root should contain a `games` folder.
4. Insert the card and restart the CYD.

See `docs/SD_CONTENT_SPEC.md` for the editable Memory and Counting schemas.

## Add Another Game

1. Create `src/games/MyGame.h` and `src/games/MyGame.cpp`.
2. Inherit from `Game` in `src/engine/Game.h`.
3. Implement `title()`, `begin()`, `update()`, and `render()`.
4. Add an instance, launcher entry, and `launch()` case in `src/main.cpp`.
5. If it needs SD data, add a typed loader method to `src/engine/ContentLoader.*` and document the JSON file in `docs/SD_CONTENT_SPEC.md`.

The engine is intentionally simple: games own their state, draw with `TFT_eSPI`, and receive touch events from the shared board layer.

## Scores

Scores are saved in ESP32 non-volatile storage, not on the SD card. Reflashing normal firmware keeps them; erasing flash clears them. Tic-Tac-Toe stores X/O/draw totals because it is a two-player game, while the other games store a best score or best low score.

## Legal Notice

(C) GoodTime Micro Company™. Copyright 2026. Trademark owned by the company.

## Game Notes

- `Time`: shows an analog clock and asks the child to choose the matching text time. It starts with hour times, then adds half-hour, quarter-hour, five-minute, and minute-level questions.
- `Multiplication Tables`: quick-fire facts from 1 through 12 with four answer choices. Difficulty starts with the 2s, 5s, and 10s, then adds the harder tables.
- `Whack A Mole`: a 9x9 grid where the child taps the smiley face before it moves. The speed increases with score and 10 misses in a row ends the round.
- `Money`: drawn US coins teach totals, making an amount, comparing coin groups, and making change. Difficulty ramps from pennies/nickels to quarters, half-dollars, and bigger amounts.
- `Fraction Circles`: drawn pie charts teach matching fractions, matching pies, and comparing fractions. Difficulty starts with halves/quarters, adds thirds/eighths, then unlike-fraction comparisons.
- `Simon`: watch the color sequence, then repeat it. The sequence grows by one after each success.
- `Sudoku`: starts with 2x2, advances to 4x4, then 6x6.
- `Maze`: thirty built-in mazes unlock one at a time. Each maze is checked for start-to-exit solvability before play.
- `Sorting`: tap number tiles in order; correct taps lock and wrong taps flash red.
- `Color Mix`: choose the mixed color swatch for simple combinations such as red plus blue.
- `Sliding Puzzle`: starts at 2x2 and advances to a solvable 3x3 number puzzle.
- `Odd One Out`: find the one shape/color that differs as rounds get subtler.

## Common Gotchas

- Touch feels offset: flash `bringup` and tap `Calibrate`.
- Screen orientation is wrong: change `CYD_SCREEN_ROTATION` in `platformio.ini`.
- SD card does not mount: use FAT32, retry from bring-up, and confirm the `games` folder is at card root.
- No sound: expected. Audio feedback is disabled and hidden in this firmware.
- Upload fails: try a different USB cable or lower `upload_speed` in `platformio.ini` to `115200`.

## Reference Hardware Notes

The pin setup follows the CYD community pin documentation: TFT on HSPI GPIO 13/12/14/15/2/21, SD on VSPI GPIO 23/19/18/5, touch on GPIO 32/39/25/33/36, RGB LED on GPIO 4/16/17, and speaker amp on GPIO 26.
