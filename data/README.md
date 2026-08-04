# SD Card Example Data

Copy the contents of `data/sd` to the root of a FAT32 microSD card.

After copying, the card root should contain:

```text
games/
```

The firmware reads memory settings from `games/memory/default.json` and counting settings from `games/counting/default.json`.
