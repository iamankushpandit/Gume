# Notice

Two different things are claimed on this project, under two different bodies of
law, and this file exists to keep them apart. Conflating them is how a project
ends up either giving away a name it meant to keep or appearing to claw back a
licence it already granted.

## Copyright

Copyright © 2026 iamankushpandit.

The code in this repository is licensed **GPL-3.0-or-later** — see
[LICENSE](LICENSE). That grant is the operative one, it is irrevocable for the
versions it has been applied to, and **nothing further down this page takes any
of it back.** You may use, study, modify and redistribute the code, and if you
distribute a modified build or a device running one, you must offer the
corresponding source on the same terms.

The copyright is held by an individual rather than by the brand below. A
trading name that is not an incorporated entity cannot hold a copyright, and a
licence notice naming a holder that does not legally exist is precisely the
notice a downstream user cannot rely on.

## Trademarks

**Braino!** is a product of GoodTime Micro Company™. **Braino!**, **GoodTime
Micro Company™** and the game names are trademarks of GoodTime Micro Company™.
The console has been renamed before; the owner did not change.

Trademark is not copyright, and the GPL does not license a name — it never has,
for any project. So the freedom you have over this code is complete, and it is
simply not the thing this section is about. What this section is about is
whether a stranger holding a device can tell whose firmware is answering them.

## If you fork it

**You are meant to fork this.** Porting Braino! to another board is the single
thing the project most wants from a contributor, and the GPL was chosen exactly
so those ports stay available to the people holding the hardware.
[docs/PORTING.md](docs/PORTING.md) is the checklist. So rather than leave you to
guess where the line is, here it is.

**Rename before you distribute** a modified build, a device running one, or an
installer page serving one:

| | |
|---|---|
| **Change** | The product identity in [`include/AppVersion.h`](include/AppVersion.h) — `BRAINO_PRODUCT_NAME` and both copyright strings. That one header is the only place the firmware spells either, so it is a one-file rename by design. Also the BLE beacon's `NAME_PREFIX` in `src/hal/BleBeacon.h`, your installer page, and the case artwork. |
| **Keep freely** | Everything else in the source. Game titles as identifiers, file and symbol names, `AppMetadata` strings you have not changed, this repository's documentation, and any factual statement that your work derives from Braino!. |

Attribute to the author, not to the brand. A fork that says

> based on [Braino!](https://github.com/iamankushpandit/Gume) by
> [iamankushpandit](https://github.com/iamankushpandit)

is accurate attribution and is always fine — that is nominative use, and no
permission is needed for it. Naming the author rather than the company is
also the more useful line for whoever reads it: the copyright, the code and
the person who will answer an issue are all in the same place, and the link
goes somewhere a reader can actually follow. The one thing asked of you is
that you not ship to strangers *as* Braino!.

Contributions **to this repository** need no trademark permission at all. The
names stay where they are, and opening a pull request grants nothing beyond the
GPL terms already described in
[CONTRIBUTING.md](CONTRIBUTING.md#licensing).

## Bundled work

Every asset compiled into the firmware is MIT or public domain, and the
libraries keep their own permissive licences. The full table, including the
one attribution obligation that survives (`mledoze/countries`, ODbL), is in the
README's [Credits and licensing](README.md#credits-and-licensing) section.
