# SD Content Spec

Put all files under a `games` folder at the root of the microSD card.

```text
/games
  /memory
    default.json
  /counting
    default.json
```

The current app has no quiz game. SD content is used only for Memory and Counting settings.

## Memory Game Config

Location: `/games/memory/default.json`

```json
{
  "type": "memory_config",
  "rows": 4,
  "cols": 6,
  "symbols": ["A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L"]
}
```

Rules:

- `rows` must be 2-4.
- `cols` must be 2-6.
- `rows * cols` must be even.
- The firmware uses up to 12 pairs.

## Counting Config

Location: `/games/counting/default.json`

```json
{
  "type": "counting_config",
  "min": 1,
  "max": 12
}
```

Rules:

- `min` must be at least 1.
- `max` is capped at 20 so the objects stay visible on the small screen.

