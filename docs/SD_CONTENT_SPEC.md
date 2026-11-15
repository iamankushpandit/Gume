# SD Content Spec

Put all files under a `games` folder at the root of the microSD card.

```text
/games
  /memory
    default.json
```

SD content is used only for Memory's settings. Counting read a number range
from `/games/counting/default.json` until it became a course with levels of
its own; a file left there is ignored.

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

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit -->

---

*Part of [Braino!](https://github.com/iamankushpandit/Gume) by [iamankushpandit](https://github.com/iamankushpandit). Copyright © 2026 iamankushpandit, licensed [GPL-3.0-or-later](https://github.com/iamankushpandit/Gume/blob/main/LICENSE) alongside the code — reuse of this document, in whole or in part, must keep this attribution and stay under the same licence. See [NOTICE.md](https://github.com/iamankushpandit/Gume/blob/main/NOTICE.md).*
