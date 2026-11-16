# host/fonts/

The typeface the Linux build draws its text in, at the panel's own
resolution, instead of the ESP32's bitmap fonts. It is compiled into the
`braino` binary (`host/cmake/EmbedFont.cmake`), so the PC build and the
RG40XX V image carry it without installing anything.

| File | What it is |
|---|---|
| `FreeSans-Latin.ttf` | GNU FreeFont's FreeSans, cut down to the characters the firmware can draw |
| `COPYING` | GNU FreeFont's licence: the GNU GPL, version 3, as shipped with the font |

## Licence

GNU FreeFont is free software under the **GNU General Public License,
version 3 or (at your option) any later version**, with the **font
exception**: a document that embeds the font is not thereby covered by the
GPL. That is the same licence family as Braino (GPL-3.0-or-later), so the
font and the program that embeds it are under compatible terms with no
further conditions. The full text is `COPYING` beside this file.

Copyright of the typeface: the GNU FreeFont contributors
(<https://www.gnu.org/software/freefont/>), as listed in the release's
`AUTHORS` and `CREDITS`.

## Where it came from, and what was changed

- Source: `https://ftp.gnu.org/gnu/freefont/freefont-ttf-20120503.zip`,
  SHA-256 `7c85baf1bf82a1a1845d1322112bc6ca982221b484e3b3925022e25b5cae89af`.
- `FreeSans.ttf` in it: SHA-256
  `c80858440d8fb618e0ac5ff6f16251dbfa6b3316f00f3cdd17d477297dd87b04`, 1,563,256
  bytes.
- **Modified**: subset to Basic Latin, Latin-1 and a few punctuation marks,
  arrows and math signs, with hinting removed, because the firmware draws
  nothing else and the whole font is 1.5 MB. Made with fontTools 4.59.0:

  ```bash
  python -m fontTools.subset FreeSans.ttf \
    --unicodes="U+0020-007E,U+00A0-00FF,U+2013-2014,U+2018-201D,U+2022,U+2026,U+20AC,U+2190-2193,U+2212,U+00D7,U+00F7" \
    --no-hinting --layout-features='kern' --name-IDs='*' --name-legacy \
    --output-file=FreeSans-Latin.ttf
  ```

  Result: SHA-256
  `fbb6d2fb75dce0f431e3a6f1b85a4ddc5fd1d435e5554ebb6efb4c812a094abe`, 17,592
  bytes. The font's own name table is kept unchanged.

To add a character, rerun the command with it added to `--unicodes`, from the
same source file, and update the hash here.

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit -->

---

*Part of [Braino!](https://github.com/iamankushpandit/Gume) by [iamankushpandit](https://github.com/iamankushpandit). Copyright © 2026 iamankushpandit, licensed [GPL-3.0-or-later](https://github.com/iamankushpandit/Gume/blob/main/LICENSE) alongside the code — reuse of this document, in whole or in part, must keep this attribution and stay under the same licence. See [NOTICE.md](https://github.com/iamankushpandit/Gume/blob/main/NOTICE.md).*
