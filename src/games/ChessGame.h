#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& chessAppMetadata();

/* Chess for two players sharing the device.
 *
 * There is no computer opponent, and that is a design decision rather than a
 * missing feature. A search deep enough to be worth playing would have to run
 * across frames or on its own task, and the whole of chess without it is just
 * rules: move generation, check, and the three ways a game ends. Two children
 * over one console is also the thing this device is actually good at.
 *
 * Tap a piece and every square it may legally move to is marked. That is the
 * requested behaviour and it is not a convenience: at 26px a square on a
 * resistive panel, tap-to-select then tap-to-destination is far more forgiving
 * than a drag, because a mis-tap just selects something else and costs
 * nothing. It also happens to be how you teach the game.
 *
 * "Legally" is meant strictly. Moves are generated pseudo-legally and then
 * filtered by making each one and asking whether the mover's own king is
 * attacked -- so pins, discovered checks and the obligation to answer a check
 * all fall out of one rule rather than being special cases. That same filter
 * is what makes checkmate and stalemate detectable: no legal moves and in
 * check is mate, no legal moves and not in check is a draw.
 *
 * Orientation-adaptive (followsLayout). The board is square, so it is sized
 * from the SHORTER panel axis and the status line takes the leftover -- which
 * makes portrait the better orientation, not the worse one: 240x320 gives a
 * 240px board and 30px squares against landscape's 26px.
 *
 * No score. A win is not a number, and a "best" would be meaningless.
 */
class ChessGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

    /* Piece codes. Sign carries colour, magnitude carries kind, so an empty
     * square is 0 and `-p` is the same piece in the other colour. That makes
     * "is this mine" a sign test and "what is it" an abs(), which is most of
     * what move generation asks. */
    enum : int8_t {
        EMPTY = 0,
        PAWN = 1,
        KNIGHT = 2,
        BISHOP = 3,
        ROOK = 4,
        QUEEN = 5,
        KING = 6,
    };

private:
    static constexpr uint8_t NO_SQ = 0xFF;
    /** Longest legal move list from one square: a queen on an open board. */
    static constexpr uint8_t MAX_MOVES = 28;

    /* One position. Small enough to copy for the legality filter, which is why
     * make/unmake is a struct assignment here rather than an undo stack. */
    struct Position {
        int8_t sq[64];        // +white, -black, 0 empty
        bool whiteToMove;
        /* Castling rights, in the order KQkq. Cleared when the king or the
         * rook in question moves or is captured -- rights are lost forever,
         * not just while something sits in the way. */
        bool castle[4];
        /* The square a pawn may capture onto en passant, or NO_SQ. Set only
         * for the one move immediately after a double push, which is why it is
         * part of the position rather than a flag on the pawn. */
        uint8_t epSquare;
    };

    enum class Status : uint8_t { Playing, Check, Checkmate, Stalemate };

    // ---- rules ----------------------------------------------------------
    static bool isWhite(int8_t piece) { return piece > 0; }
    static int8_t kind(int8_t piece) { return piece < 0 ? -piece : piece; }
    static bool onBoard(int8_t file, int8_t rank) {
        return file >= 0 && file < 8 && rank >= 0 && rank < 8;
    }
    /* Every move `from` can make without regard to leaving the king exposed. */
    static uint8_t pseudoMoves(const Position& p, uint8_t from, uint8_t* out);
    /** True when `bySideIsWhite` attacks `square` in `p`. */
    static bool attacked(const Position& p, uint8_t square, bool bySideIsWhite);
    static uint8_t kingSquare(const Position& p, bool white);
    /** Apply a move, including castling, en passant and auto-promotion. */
    static void applyMove(Position& p, uint8_t from, uint8_t to);
    /* Legal moves: pseudo-legal, minus any that leave the mover in check. */
    static uint8_t legalMoves(const Position& p, uint8_t from, uint8_t* out);
    static bool hasAnyLegalMove(const Position& p);

    // ---- layout, all measured from the live panel ------------------------
    Rect boardRect(AppContext& host) const;
    Rect squareRect(AppContext& host, uint8_t square) const;
    uint8_t squareAt(AppContext& host, int16_t x, int16_t y) const;

    void drawSquare(AppContext& host, uint8_t square) const;
    void drawPiece(AppContext& host, const Rect& r, int8_t piece) const;
    void drawStatus(AppContext& host) const;
    void refreshStatus();

    Position pos_{};
    Status status_ = Status::Playing;
    uint8_t selected_ = NO_SQ;
    uint8_t targets_[MAX_MOVES] = {};
    uint8_t targetCount_ = 0;
    /* Squares needing repaint next frame. A move changes at most a handful of
     * squares, and a full board is 64 fills plus 32 glyphs -- far more than a
     * frame wants to spend when two of them changed. */
    uint8_t dirtySq_[MAX_MOVES + 4] = {};
    uint8_t dirtyCount_ = 0;
    bool statusStale_ = true;

    void markSquare(uint8_t square);
};
