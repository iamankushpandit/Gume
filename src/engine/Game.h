#pragma once

#include <Arduino.h>
#include "engine/AppCapabilities.h"
#include "hal/Sound.h"
#include "hal/TouchTypes.h"
#include "ui/Renderer.h"

struct AppDefinition;
class Board;
class ContentLoader;

/* One seat at a two-player game on another console.
 *
 * Everything an app is allowed to know about a peer: the hardware id it
 * already broadcasts about itself, and whether it is currently offering or
 * playing a game. No name, no profile, no label -- a game does not need them
 * and cannot be trusted with them. */
/* One console in the room, as a two-player game sees it.
 *
 * `deviceId` is the four hex characters the peer advertises and is what the
 * rest of this API matches on. `name` is the owner's OWN label for that
 * console if they have given it one, resolved locally and never transmitted --
 * see Board::peerName(). Show `name` when it is set and `deviceId` when it is
 * not; a game should not have to know that distinction exists beyond this
 * line. */
struct NearbySeat {
    char deviceId[5] = {0};
    char name[11] = {0};        // local label, empty when unnamed
    bool inviting = false;      // offering us a game right now
    /* The offer is for the app that is open on THIS console. An invitation
     * names its game, and a lobby must not offer to accept one for another --
     * a Sea Battle invitation answered from the Chess lobby is two consoles
     * playing different games at each other. Only meaningful with `inviting`. */
    bool forThisGame = false;
    uint8_t session = 0;
    /* Which side we take if we accept. The console that offers the game flips
     * for it rather than keeping the advantage of moving first, and the answer
     * travels with the invitation so there is nothing to negotiate. Every
     * two-player game needs this and none of them should be inventing it. */
    bool weMoveFirst = false;
};

/* The opponent's latest move, as heard on the air. `ack` is the highest ply of
 * OURS they have applied, which is how a sender knows to stop worrying about
 * a move that may have been missed. */
struct NearbyTurn {
    uint8_t session = 0;
    uint8_t ply = 0;
    uint8_t from = 0;
    uint8_t to = 0;
    uint8_t ack = 0;
    /* They stopped the game. Every two-player game needs a way to say "I
     * cannot finish this", and every one of them would otherwise invent its
     * own reserved value -- so the service owns the encoding and reports it as
     * a flag. A game must treat this as the end of the session and must not
     * read `from`/`to` when it is set. */
    bool ended = false;
};

class AppContext {
public:
    virtual ~AppContext() = default;
    virtual Ui::Renderer& display() = 0;
    virtual ContentLoader& content() = 0;
    virtual uint32_t getScore(const char* key, uint32_t fallback = 0) = 0;
    virtual void setScore(const char* key, uint32_t value) = 0;
    virtual bool saveBestScore(const char* key, uint32_t value, bool lowerIsBetter) = 0;
    virtual void loadBlob(const char* key, void* dst, size_t len) = 0;
    virtual void saveBlob(const char* key, const void* src, size_t len) = 0;
    virtual void beepOk() = 0;
    virtual void beepError() = 0;
    /* The rest of the console's sound vocabulary -- hal/Sound.h says what
     * each cue means, and a game should pick the one that matches what just
     * happened rather than the one that sounds nicest. Silent on a board with
     * no codec, so nothing may depend on it having been heard. */
    virtual void playSound(Sound cue) = 0;
    virtual void pulseRgb(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) = 0;
    virtual void drawTopBar(const char* title) = 0;
    virtual void goHome() = 0;
    virtual void relaunchActiveGame() = 0;

    /* ---- two-player games on nearby consoles ---------------------------
     *
     * Deliberately MOVE-shaped and not byte-shaped. An app can say "I played
     * from square X to square Y"; it cannot say "put these bytes on the air".
     * That is the whole reason this sits in AppContext at all rather than
     * handing a game the radio: the sandbox is what would make a third-party
     * app safe to run one day, and it survives only if every hole in it is
     * this narrow. Nothing here can transmit a name, a profile or a score.
     *
     * All of it is inert unless the owner has switched on both the beacon and
     * Nearby play. A game must therefore treat every call as best-effort and
     * never require one to have succeeded.
     *
     * THIS IS A SERVICE, NOT A CHESS FEATURE. Everything here is stated in
     * the terms every two-player game shares: who is in the room, who offered
     * whom a game, which of the two moves first, one numbered move at a time,
     * and either side ending it. Nothing here knows what a move means, and the
     * two places a game would otherwise have to invent something -- deciding
     * sides, and saying "I give up" -- are both answered here so that the next
     * game does not answer them differently. If a future game needs something
     * this cannot express, widen it here rather than reaching past it. */
    virtual uint8_t nearbySeatCount() = 0;
    virtual bool nearbySeatAt(uint8_t index, NearbySeat& out) = 0;
    /* Offer a game to one peer. False if the radio is off or the id is bad.
     * `weMoveFirst` is set on success and is decided HERE, by a coin flip, so
     * that the console doing the asking does not also get first move -- and so
     * that no game has to remember to be fair on its own. */
    virtual bool nearbyInvite(const char* deviceId, uint8_t session,
                              bool& weMoveFirst) = 0;
    /** An invitation aimed at THIS device, if one is on the air. */
    virtual bool nearbyInviteForUs(NearbySeat& out) = 0;
    /* Publish our latest move and keep it on the air until it is replaced.
     * Cheap to call every frame: unchanged values do not touch the radio. */
    virtual void nearbyPublish(uint8_t session, uint8_t ply, uint8_t from,
                               uint8_t to, uint8_t ack) = 0;
    /* Tell the other seat we are stopping. Arrives as NearbyTurn::ended, and
     * like a move it stays on the air until something replaces it, so it
     * cannot be the one message that goes missing. */
    virtual void nearbyEnd(uint8_t session, uint8_t ply, uint8_t ack) = 0;
    /** Stop advertising a game. */
    virtual void nearbyStop() = 0;
    /** The named peer's latest move in `session`, if it has one on the air. */
    virtual bool nearbyTurnFrom(const char* deviceId, uint8_t session,
                                NearbyTurn& out) = 0;
    /* This console's own tag, as its peers see it -- the four hex digits it
     * already advertises about itself -- or "" while sessions are not allowed.
     * A game with more than two seats needs it to put every console, itself
     * included, in the same order on every console. It is the same identifier
     * every peer already has, so handing it to an app reveals nothing new. */
    virtual const char* nearbySelfId() = 0;
};

class GameHost : public AppContext {
public:
    virtual ~GameHost() = default;
    // Reserved for system screens that genuinely need full device authority.
    virtual Board& board() = 0;
    virtual bool hasCapability(uint32_t capability) const = 0;
    virtual bool requireCapability(uint32_t capability, const char* action) = 0;
    virtual uint8_t launcherEntryCount() = 0;
    virtual uint8_t launcherPageSize() = 0;
    virtual const AppDefinition& launcherEntry(uint8_t filteredIndex) = 0;
    virtual void openApp(const AppDefinition& app) = 0;
    virtual void openSettings() = 0;
    virtual void openWifi() = 0;
    virtual void openProfiles() = 0;
};

class Game {
public:
    virtual ~Game() = default;
    virtual const char* title() const = 0;
    virtual void begin(GameHost& host) = 0;
    virtual void update(GameHost& host, const TouchPoint& touch) = 0;
    /* TWO-PHASE RENDER. Override these two rather than render().
     *
     * renderStatic()  runs ONLY when needsFullRender(). Background, chrome,
     *                 grid lines, fixed labels -- anything that does not change
     *                 while the screen is up. Ui::clear() belongs here and
     *                 ONLY here.
     * renderDynamic() runs on EVERY repaint. It must not clear the screen, and
     *                 it is responsible for erasing its own previous output --
     *                 there is no framebuffer, so nothing else will.
     *
     * A full 320x240 repaint is ~150KB over SPI and about 30ms of blanking,
     * against a 20ms frame budget; on the 480x320 board it is twice that, so
     * three frame budgets to change one tile. The two-level dirty flags have
     * always said which was needed and were ignored, because a single render()
     * that opens with Ui::clear() throws the distinction away one line later.
     * Splitting the method is what makes the model structural instead of
     * advisory -- there are two functions, so a caller has to choose.
     *
     * The default render() below dispatches, so a screen that has not been
     * converted yet keeps its own render() override and behaves exactly as it
     * did. Conversion is per screen and both forms coexist. */
    /* A SCREEN TRANSITION IS ALWAYS A FULL REPAINT, AND THAT IS NOT
     * NEGOTIABLE. Every screen is a static instance reused for the life of the
     * device, so it arrives carrying the flags its LAST visit left -- and the
     * last thing that visit did was clearDirty(). The runtime therefore calls
     * requestRender() on every entry path (launch, goHome, relaunch); a
     * screen's begin() must never be relied on to do it.
     *
     * Getting this wrong does not look like a missing optimisation. It looks
     * like a screen with no top bar and the previous screen showing through
     * it, because renderStatic() is where both Ui::clear() and the chrome
     * live. Partial repaint is an optimisation WITHIN a screen's lifetime.
     * Entering one is not the place to be greedy: a stable picture is worth
     * more than the frame it costs. */
    virtual void renderStatic(GameHost& host) { (void)host; }
    virtual void renderDynamic(GameHost& host) { (void)host; }

    virtual void render(GameHost& host) {
        if (needsFullRender()) {
            renderStatic(host);
        }
        renderDynamic(host);
    }

    /* Called exactly once when the screen is left, before the next screen's
     * begin(). Default does nothing, which is correct for the games -- they
     * hold only their own member state and it is reset by begin().
     *
     * Override it if a screen acquires anything that outlives a frame: a
     * cached buffer, a sampling cadence, a radio or file handle. Nothing here
     * runs off a task or timer today, so nothing keeps burning cycles after
     * you leave it; this hook exists so that stays true as screens grow. */
    virtual void end(GameHost& host) { (void)host; }

    bool needsRender() const {
        return dirty_;
    }

    void clearDirty() {
        dirty_ = false;
        fullRedraw_ = false;
    }

    void requestRender() {
        dirty_ = true;
        fullRedraw_ = true;   // coming back from elsewhere: repaint everything
    }

    /* Repaint the header strip alone, leaving the screen under it untouched.
     *
     * The battery badge, the clock and the notification banner all change on
     * their own schedule rather than the screen's, and the runtime used to
     * answer that with requestRender() -- a full 320x240 wipe, ~150KB over SPI
     * and roughly 30ms of visible blanking, for a change confined to the top
     * 30 pixels. The strip is an eighth of the panel, so this costs about 4ms
     * and fits inside the frame budget where a full repaint is 150% of it.
     *
     * It mattered most for the battery: one percent is about 2mV on the LiPo
     * plateau, under two ADC counts, so the reading crosses a boundary every
     * couple of seconds -- and with the BLE beacon advertising, its supply
     * ripple made that constant. The whole screen flashed each time.
     *
     * The default is the standard top bar, which is what every screen that has
     * one draws, always with title(). A screen carrying its own header --
     * LauncherApp, ProfileApp -- overrides this. Returning false means "I
     * cannot repaint my chrome in isolation"; the runtime falls back to a full
     * repaint, so a screen that is unsure should say so rather than guess. */
    virtual bool renderChrome(GameHost& host) {
        host.drawTopBar(title());
        return true;
    }

protected:
    /* Two levels of invalidation.
     *
     * Every game used to clear the whole 320x240 screen on any change. At
     * 40MHz SPI that is ~150KB pushed and roughly 30ms of visible wipe before
     * anything is drawn again, which is what made the UI flicker -- and in
     * Cinnamon's case flash hard enough to be a photosensitivity concern.
     *
     * markDirty()     content changed; repaint the moving parts only.
     * markFullDirty() layout/structure changed; repaint the background too.
     *
     * A render() should paint its static chrome under `if (needsFullRender())`
     * and its dynamic parts unconditionally. */
    void markDirty() {
        dirty_ = true;
    }

    void markFullDirty() {
        dirty_ = true;
        fullRedraw_ = true;
    }

    bool needsFullRender() const {
        return fullRedraw_;
    }

private:
    bool dirty_ = true;
    bool fullRedraw_ = true;   // first paint is always a full one
};

class AppGame : public Game {
public:
    virtual void begin(AppContext& host) = 0;
    virtual void update(AppContext& host, const TouchPoint& touch) = 0;
    /* The AppContext half of the two-phase render -- see Game. A catalog game
     * overrides these two; the render() below dispatches. */
    virtual void renderStatic(AppContext& host) { (void)host; }
    virtual void renderDynamic(AppContext& host) { (void)host; }

    virtual void render(AppContext& host) {
        if (needsFullRender()) {
            renderStatic(host);
        }
        renderDynamic(host);
    }
    virtual void end(AppContext& host) { (void)host; }

    void begin(GameHost& host) final {
        begin(static_cast<AppContext&>(host));
    }

    void update(GameHost& host, const TouchPoint& touch) final {
        update(static_cast<AppContext&>(host), touch);
    }

    void render(GameHost& host) final {
        render(static_cast<AppContext&>(host));
    }

    void end(GameHost& host) final {
        end(static_cast<AppContext&>(host));
    }
};
