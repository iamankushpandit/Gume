# Case — the 2.8-inch boards

A complete printed shell for the 2.8-inch boards: a front tray the board drops
into, a back plate with a speaker grille, and four printed clips that hold the
two together. **No metal hardware at all** — the shell it replaces needed four
M3 × 25 screws and four washers, about $5.36 a unit, which was a noticeable
fraction of what the board costs.

This folder is named for the E32R28T-1 because that is the board Braino ships
against, but the same parts fit the other 2.8-inch boards in this repo — see
[Which boards](#which-boards) below.

<p align="center">
  <img src="preview.png" width="420" alt="The four printed parts on one plate: front tray, back plate with speaker grille, and four clips">
</p>

## What is here

| File | What it is |
|---|---|
| `BrainoCase-v8.stl` | All four parts on one plate. Open this in any slicer. |
| `preview.png` | Render of the plate, straight from the mesh. |

Four parts, 192.8 × 84.0 mm on the plate — they fit any bed from 200 mm up:

| Part | Size | Role |
|---|---|---|
| Front tray | 91.4 × 55.4 × 18.8 mm | The board drops in display-forward, with the USB connector through the side cut-out |
| Back plate | 91.4 × 55.4 × 25.0 mm | Closes the shell; carries the round grille for a **20 mm speaker** and the posts that locate it |
| Clips ×4 | 10.0 × 7.6 × 5.0 mm | C-shaped fasteners that pinch the two halves together at the corners |

## Print settings

**Not recorded yet.** The parts have been printed and fitted, but the profile
they were sliced with was not written down, so there is nothing honest to put
in this table — the numbers in this folder's previous version described a
different case entirely and were carried over from its `.3mf`.

If you print these, add what you used: layer height, material, whether you
needed supports, and what went wrong on the first attempt. That last one is the
most useful line in any of these files.

## Assembly

The board sits in the front tray with the display forward and the USB connector
through the side cut-out. A 20 mm speaker sits behind the grille in the back
plate. The two halves go together and the four clips hold them at the corners.

There is no dedicated battery compartment: the single-cell Li-ion/LiPo pack the
board charges (see the Battery section of the [README](../../README.md)) goes in
the free space by hand.

## Which boards

Braino supports four 2.8-inch board names — `E32R28T-1`, `ESP32-2432S028R`,
`ESP32-2432S028-inv` and `ESP32-2432S028Rv3` — and they share this outline, so
this shell is the case for all of them. That is the maintainer's own fitting on
the 2.8-inch boards to hand, not a measurement of every variant: if yours does
not fit, that is worth [an issue](https://github.com/iamankushpandit/Gume/issues),
because it means the family is less uniform than this file claims.

The 3.2-inch [E32R32P](../E32R32P/) and the 4-inch [E32R40T](../E32R40T/) have
their own folders. Do not print this one for either.

## Status

**Printed and fitted**, and it is the case this project recommends for a
2.8-inch build. It supersedes the two-part screwed shell that was here before,
which was explicitly a stopgap: expensive fasteners that stuck out past the
back, no speaker mount, and no light pipe for the RGB LED.

Still open, and still worth reading before a redesign:
[issue #13](https://github.com/iamankushpandit/Gume/issues/13) lists the
constraints the firmware imposes on the mechanical design — the LED is the only
non-visual feedback channel and has to be visible, the resistive panel needs
backing from behind, USB and BOOT must stay reachable without opening the case,
and the battery must need a tool.
