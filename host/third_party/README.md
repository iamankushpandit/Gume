# host/third_party/

Other people's code, kept **verbatim** under its own licence, compiled into
the Linux build only. `tools/check_licenses.py` exempts this directory for
that reason: stamping Braino's notice on these files would claim them.

| File | Version | Licence |
|---|---|---|
| `stb/stb_truetype.h` | stb_truetype v1.26, from `https://raw.githubusercontent.com/nothings/stb/f58f558c120e9b32c217290b80bad1a0729fbb2c/stb_truetype.h` (SHA-256 `ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab`) | Public domain or MIT, at the user's choice; the text is at the end of the file |

`stb_truetype.h` rasterises the font in `host/fonts/` (`host/src/HostText.cpp`).
It is vendored rather than fetched because the RG40XX V image builds
offline from packages already in its OS base, and a new Buildroot package
would change that base.
