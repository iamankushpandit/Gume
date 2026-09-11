# Case — E32R32P (3.2-inch)

A two-part enclosure for the E32R32P, the 3.2-inch ST7789 board: a front shell
that frames the display, and a back plate with a grille for an 8 Ω speaker.

<p align="center">
  <img src="preview.png" width="420" alt="The front shell and the back plate with its speaker grille">
</p>

## What is here

| File | What it is | Size |
|---|---|---|
| `FrontWithFasteners.stl` | Front shell, with the fasteners on the same plate | 99.1 × 89.0 × 25.0 mm |
| `BackSpeakerGrill.stl` | Back plate with a grille over an 8 Ω speaker | 99.1 × 60.4 × 18.8 mm |
| `preview.png` | Render of both meshes, generated from the STLs | |

The files were named `Best3.5FrontEnclosurewithFastners.stl` and
`good3.5back8ohmspeakergrill.stl` when designed; "3.5" there is the design's
working name, not the panel size. They are sized for the E32R32P.

## Print settings

Not recorded yet. If you print it, add the layer height, material, supports and
anything you had to change here -- see [the cases README](../README.md) for
what is most useful.

## Assembly

The speaker sits behind the grille on the back plate; on this board sound
comes from GPIO26 through the onboard amplifier to the speaker connector.
Not written up yet: how the board and the printed fasteners go together, how
the battery is held, and whether any screws are needed.

## Status

**Printed and fitted** by the maintainer with an E32R32P inside.
