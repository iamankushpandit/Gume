#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

struct AppDefinition;

/* The one question the runtime asks on the way back into a game that was
 * left with something in it: carry on, or start a new one.
 *
 * It is the runtime's own screen, not a game's, so that forty games do not
 * each draw one and no game can forget to. It is shown as the game it stands
 * in front of -- the same title in the top bar, the same canvas and rotation
 * -- and takes no decision itself: the runtime reads take() after update()
 * and either resumes the game or begins it. See Game::keepsGame(). */
class CarryOnScreen : public Game {
public:
    enum class Choice : uint8_t { None, CarryOn, NewGame };

    /** Which game is waiting. Call before begin(). */
    void offer(const AppDefinition& app) { app_ = &app; }
    /** What was pressed since the last call; None until something is. */
    Choice take() {
        const Choice chosen = choice_;
        choice_ = Choice::None;
        return chosen;
    }

    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void renderStatic(GameHost& host) override;
    /* Leaving the question is not leaving a game: the game it asks about is
     * still waiting, and this screen has nothing of its own to keep. */
    Keep keepsGame() const override { return Keep::No; }

private:
    const AppDefinition* app_ = nullptr;
    Choice choice_ = Choice::None;
};
