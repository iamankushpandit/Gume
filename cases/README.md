# Cases

Printable enclosures, one folder per board.

A case is **not required** for a board to be supported. Firmware support is
decided by `platformio.ini` and `include/BoardConfig.h`, and a board with no
folder here is a perfectly good port. This directory exists because the person
most likely to build this is someone who has never designed an enclosure and
does not want to — handing them a bare board with a battery taped to the back
is a worse package than handing them a file they can print. So: a case is
encouraged, and its absence is not a defect.

```
cases/
  <BOARD_NAME>/          exactly the -D BOARD_NAME string from platformio.ini
    README.md            print settings, hardware needed, what fits and what doesn't
    preview.png          a render or a photo of the printed parts
    *.stl                the meshes, ready to slice
    *.3mf                optional: the project with slicer settings intact
```

| Board | Case | Notes |
|---|---|---|
| [E32R28T-1](E32R28T-1/) | Two-part screwed shell | Fits the 2.8-inch ILI9341 board Braino targets today |

## Adding a case for your board

1. Create `cases/<BOARD_NAME>/`, using the same string the firmware reports —
   the `-D BOARD_NAME=\"…\"` value in `platformio.ini`. The About app shows it
   on the device, so a builder can match a folder to the thing in their hand.
2. Put the meshes in as **STL**, and the slicer project alongside them if you
   have one. STL is what every slicer opens; a 3MF is what lets someone
   reproduce your print rather than guess at it.
3. Write the folder's `README.md`. The parts of it that matter are the ones a
   printer cannot infer: layer height, whether supports are needed, what
   fasteners you used, how the battery is held, and — most useful of all —
   **what you got wrong on the first print**.
4. Include a picture. A render from the slicer is fine; a photo of the printed
   case with the board in it is better.
5. Add a row to the table above.
6. Say whether you have actually printed and fitted it, or whether it is
   modelled from a drawing. Both are worth having. Confusing the two wastes
   someone else's filament.

Cases are covered by the same contribution route as the firmware — fork, branch
off `dev`, open a pull request. See [CONTRIBUTING.md](../CONTRIBUTING.md).

Model files in this directory are released under the same licence as the rest
of the project unless a folder's own README says otherwise.
