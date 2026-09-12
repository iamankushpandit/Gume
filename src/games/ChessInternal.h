#pragma once

/* Shared by the Chess*.cpp files and nobody else. ChessGame.cpp was one file
 * until it passed the size the modularity rule allows; the handful of helpers
 * more than one of the pieces needs live here rather than being stated twice.
 * The public surface is ChessGame.h. */

#include "ChessGame.h"

/* Named idx() and not the obvious sq(): Arduino already defines sq(x) as "x
 * squared", so a two-argument call to it is a preprocessor error rather than a
 * shadowing warning, and the message names a macro you never wrote. The
 * Position member stays .sq -- member access never reaches the preprocessor. */
constexpr uint8_t idx(int8_t file, int8_t rank) {
    return static_cast<uint8_t>(rank * 8 + file);
}
constexpr int8_t fileOf(uint8_t s) { return static_cast<int8_t>(s % 8); }
constexpr int8_t rankOf(uint8_t s) { return static_cast<int8_t>(s / 8); }

/* The back rank, used to set up and to spell a promotion. */
constexpr int8_t BACK_RANK[8] = {
    ChessGame::ROOK, ChessGame::KNIGHT, ChessGame::BISHOP, ChessGame::QUEEN,
    ChessGame::KING, ChessGame::BISHOP, ChessGame::KNIGHT, ChessGame::ROOK,
};
