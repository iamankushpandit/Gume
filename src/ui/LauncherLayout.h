#pragma once

#include "hal/Board.h"
#include "ui/Ui.h"

namespace LauncherLayout {

Rect topBarSettingsRect(int16_t screenW);
int16_t headerHeight(Board::LayoutMode mode);
Rect gearRect(Board::LayoutMode mode, int16_t screenW);
Rect profileRect(Board::LayoutMode mode, int16_t screenW);
Rect tileRect(uint8_t slot, Board::LayoutMode mode, int16_t screenW, int16_t screenH);
uint8_t pageSize(Board::LayoutMode mode);

}  // namespace LauncherLayout
