#include "PianoGame.h"

#include "engine/AppRegistry.h"

namespace {

/* Score is null: there is nothing to be best at. See PianoGame.h. The field
 * list is positional and the parser in tools/app_registry_parser.py reads it
 * that way, so keep comments out from between the fields. */
constexpr AppMetadata PIANO_METADATA = {
    "piano",
    "Piano",
    nullptr,
    "play a tune",
    "Piano",
    "One octave. Tap a key, hear a note.",
    nullptr,
    LauncherIcon::Piano,
    31,
    true,
};

/* Which white key each black key sits between, as an index into the white run.
 * A keyboard has no black key between E-F or B-C, which is the whole reason
 * this is a table and not arithmetic. */
constexpr uint8_t BLACK_AFTER_WHITE[PianoGame::BLACK_KEYS] = {0, 1, 3, 4, 5};

/* Sound for each key, white run first then black, matching keyAt()'s indices.
 * Deliberately a table of vocabulary entries rather than a base plus offset:
 * the game may only sound notes that Sound.h declares, and this is what makes
 * that visible at a glance. */
constexpr Sound WHITE_SOUND[PianoGame::WHITE_KEYS] = {
    Sound::NoteC4, Sound::NoteD4, Sound::NoteE4, Sound::NoteF4,
    Sound::NoteG4, Sound::NoteA4, Sound::NoteB4, Sound::NoteC5,
};
constexpr Sound BLACK_SOUND[PianoGame::BLACK_KEYS] = {
    Sound::NoteCs4, Sound::NoteDs4, Sound::NoteFs4, Sound::NoteGs4,
    Sound::NoteAs4,
};

constexpr const char* WHITE_NAME[PianoGame::WHITE_KEYS] = {
    "C", "D", "E", "F", "G", "A", "B", "C",
};
constexpr const char* BLACK_NAME[PianoGame::BLACK_KEYS] = {
    "C#", "D#", "F#", "G#", "A#",
};

/* The keyboard leaves room for the top bar above it and a caption below. */
constexpr int16_t TOP_BAR_H = 30;
constexpr int16_t CAPTION_H = 22;
constexpr int16_t MARGIN = 4;

}   // namespace

const AppMetadata& pianoAppMetadata() {
    return PIANO_METADATA;
}

const char* PianoGame::title() const {
    return pianoAppMetadata().screenTitle != nullptr
        ? pianoAppMetadata().screenTitle
        : pianoAppMetadata().title;
}

void PianoGame::begin(AppContext& host) {
    (void)host;
    held_ = NO_KEY;
    lit_ = NO_KEY;
    drawnLit_ = NO_KEY;
    litSinceMs_ = 0;
    markFullDirty();
}

/* Every rect below is measured from the live panel. On the 4-inch in landscape
 * that is 480x320 and the keys simply get bigger; in portrait it is 240x320
 * and they get narrower and taller. Neither case is a special case. */
Rect PianoGame::keyboardRect(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    const int16_t w = static_cast<int16_t>(tft.width());
    const int16_t h = static_cast<int16_t>(tft.height());
    const int16_t top = static_cast<int16_t>(TOP_BAR_H + MARGIN);
    return Rect{MARGIN, top,
                static_cast<int16_t>(w - MARGIN * 2),
                static_cast<int16_t>(h - top - CAPTION_H - MARGIN)};
}

Rect PianoGame::whiteKeyRect(AppContext& host, uint8_t index) const {
    const Rect kb = keyboardRect(host);
    /* Divide the span rather than stepping by a fixed width, and give the last
     * key whatever the division left over -- eight keys rarely divide a panel
     * evenly, and a two-pixel gap at the right-hand edge is the kind of thing
     * that looks like a bug. */
    const int16_t x0 = static_cast<int16_t>(kb.x + (int32_t)kb.w * index / WHITE_KEYS);
    const int16_t x1 = static_cast<int16_t>(kb.x + (int32_t)kb.w * (index + 1) / WHITE_KEYS);
    return Rect{x0, kb.y, static_cast<int16_t>(x1 - x0), kb.h};
}

Rect PianoGame::blackKeyRect(AppContext& host, uint8_t index) const {
    const Rect kb = keyboardRect(host);
    const Rect left = whiteKeyRect(host, BLACK_AFTER_WHITE[index]);
    /* Centred on the boundary between its two white keys, three fifths of
     * their width, and two thirds of their height -- the proportions of a real
     * keyboard, expressed against whatever width the panel gave us. */
    const int16_t bw = static_cast<int16_t>(left.w * 3 / 5);
    return Rect{static_cast<int16_t>(left.x + left.w - bw / 2), kb.y,
                bw, static_cast<int16_t>(kb.h * 2 / 3)};
}

uint8_t PianoGame::keyAt(AppContext& host, int16_t x, int16_t y) const {
    /* Black keys first. They are drawn over the white ones and physically
     * overlap them, so testing white first would make the top two thirds of
     * every black key unreachable -- the note would be there, visible, and
     * would play its neighbour. */
    for (uint8_t i = 0; i < BLACK_KEYS; ++i) {
        if (blackKeyRect(host, i).contains(x, y, 0)) {
            return static_cast<uint8_t>(WHITE_KEYS + i);
        }
    }
    for (uint8_t i = 0; i < WHITE_KEYS; ++i) {
        if (whiteKeyRect(host, i).contains(x, y, 0)) return i;
    }
    return NO_KEY;
}

Sound PianoGame::soundFor(uint8_t key) const {
    return key < WHITE_KEYS ? WHITE_SOUND[key] : BLACK_SOUND[key - WHITE_KEYS];
}

void PianoGame::update(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();

    if (touch.justPressed) {
        const uint8_t key = keyAt(host, touch.x, touch.y);
        if (key != NO_KEY) {
            held_ = key;
            lit_ = key;
            litSinceMs_ = now;
            lastArmMs_ = now;
            host.playSound(soundFor(key));
            markDirty();
        }
        return;
    }

    if (touch.justReleased) {
        held_ = NO_KEY;
        /* The light is not released with the finger: it runs out on its own
         * timer below, so a quick tap still shows which key was struck. */
        return;
    }

    /* Held keys keep sounding. A cue runs for SOUND_NOTE_MS and then stops,
     * so a finger resting on a key went quiet while it was still pressed --
     * which is not what a piano does. Re-arming slightly before the note ends
     * leaves no audible gap: the tone has constant amplitude and the
     * synthesiser slews gain over a few milliseconds, so the seam does not
     * click. It is a re-trigger rather than true sustain, which the
     * synthesiser has no notion of. */
    if (held_ != NO_KEY && now - lastArmMs_ >= SOUND_NOTE_MS - 40) {
        lastArmMs_ = now;
        litSinceMs_ = now;
        host.playSound(soundFor(held_));
    }

    if (lit_ != NO_KEY && held_ == NO_KEY && now - litSinceMs_ >= KEY_LIT_MS) {
        lit_ = NO_KEY;
        markDirty();
    }
}

void PianoGame::drawKey(AppContext& host, uint8_t key, bool lit) const {
    Ui::Renderer& tft = host.display();
    const bool white = key < WHITE_KEYS;
    const Rect r = white ? whiteKeyRect(host, key)
                         : blackKeyRect(host, static_cast<uint8_t>(key - WHITE_KEYS));

    /* Lit uses the theme's accent rather than a fixed colour, so a key looks
     * pressed in all nine themes -- including Pocket, which has no hue to
     * spare and separates by lightness. */
    const uint16_t fill = lit ? Ui::success()
                              : (white ? Ui::rgb(248, 248, 244) : Ui::rgb(24, 24, 28));
    tft.fillRect(r.x, r.y, r.w, r.h, fill);
    tft.drawRect(r.x, r.y, r.w, r.h, Ui::outline());

    /* The note name lives on the key, low down where a black key does not
     * cover it. It is what makes this teach something rather than just make a
     * noise -- and on a board with no speaker it is the whole of the feedback. */
    const char* name = white ? WHITE_NAME[key] : BLACK_NAME[key - WHITE_KEYS];
    tft.setTextDatum(BC_DATUM);
    tft.setTextColor(lit ? Ui::bg() : (white ? Ui::rgb(60, 60, 70) : Ui::rgb(200, 200, 210)),
                     fill);
    tft.drawString(name, static_cast<int16_t>(r.x + r.w / 2),
                   static_cast<int16_t>(r.y + r.h - 4), 2);
    tft.setTextDatum(TL_DATUM);
}

void PianoGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    for (uint8_t i = 0; i < WHITE_KEYS; ++i) drawKey(host, i, false);
    for (uint8_t i = 0; i < BLACK_KEYS; ++i) {
        drawKey(host, static_cast<uint8_t>(WHITE_KEYS + i), false);
    }
    drawnLit_ = NO_KEY;

    const int16_t h = static_cast<int16_t>(tft.height());
    tft.setTextDatum(BC_DATUM);
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.drawString("Tap the keys", static_cast<int16_t>(tft.width() / 2),
                   static_cast<int16_t>(h - 4), 1);
    tft.setTextDatum(TL_DATUM);
}

void PianoGame::renderDynamic(AppContext& host) {
    if (lit_ == drawnLit_) return;

    /* Repaint only the keys that changed, then restore everything that sits on
     * top of them. A full keyboard is thirteen fills and thirteen strings,
     * which at tap speed would be most of the frame budget spent redrawing
     * keys nobody touched.
     *
     * THE ORDER HERE IS THE WHOLE THING. Black keys are painted over the white
     * ones and physically overlap them, so a white key repainted on its own
     * erases whatever black keys crossed it. The first version only restored a
     * black key if that key was itself the one lighting or unlighting, so
     * pressing a white key made its black neighbours vanish -- correct
     * arithmetic, wrong layering, and the same shape as the launcher's ghost
     * shadows and the clock's score header. */
    const uint8_t was = drawnLit_;
    const uint8_t now = lit_;
    drawnLit_ = lit_;

    for (uint8_t key : {was, now}) {
        if (key == NO_KEY) continue;
        if (key < WHITE_KEYS) {
            drawKey(host, key, key == now);
            continue;
        }
        /* A black key cannot be erased on its own either: clearing it takes a
         * bite out of the two white keys it straddles, so they go first. */
        const uint8_t left = BLACK_AFTER_WHITE[key - WHITE_KEYS];
        drawKey(host, left, left == now);
        if (left + 1 < WHITE_KEYS) {
            drawKey(host, static_cast<uint8_t>(left + 1),
                    static_cast<uint8_t>(left + 1) == now);
        }
    }

    /* Every black key, unconditionally, because any white repaint above may
     * have cut into one. Five small fills is cheaper than working out which,
     * and it cannot be got subtly wrong the way the previous version was. */
    for (uint8_t i = 0; i < BLACK_KEYS; ++i) {
        const uint8_t key = static_cast<uint8_t>(WHITE_KEYS + i);
        drawKey(host, key, key == now);
    }
}
