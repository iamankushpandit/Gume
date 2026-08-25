# Case — E32R28T-1 (ESP32-32E, 2.8-inch)

The enclosure for the board Braino targets today: the E32R28T-1 / ESP32-32E
with the ILI9341 320×240 resistive touch panel. Board details, including the
pinout this case has to leave reachable, are in
[BOARD_E32R28T-1.md](../../BOARD_E32R28T-1.md).

<p align="center">
  <img src="preview.png" width="380" alt="The two printed parts: back tray and front bezel">
</p>

## What is here

| File | What it is |
|---|---|
| `BrainoCase.stl` | Both parts, arranged on one plate. Open this in any slicer. |
| `BrainoCase.3mf` | Bambu Studio project — the same two parts with the print profile below already applied. |
| `preview.png` | Slicer render of the plate. |

Two parts, printed flat, no supports:

| Part | Size | Role |
|---|---|---|
| Back plate v2 | 53.4 × 89.4 × 7.0 mm | Tray the board drops into, with corner bosses and a side cut-out for the USB connector |
| Top plate v3 | 53.4 × 89.4 × 6.7 mm | Bezel that frames the display and clamps the board down |

Together they occupy roughly 125 × 91 × 19 mm on the build plate, so they fit
any bed from a 180 mm printer upwards on a single plate.

## Print settings

These are the settings the `.3mf` carries — the profile the parts were sliced
with, not a guess:

| | |
|---|---|
| Printer | Bambu Lab X1 Carbon, 0.4 mm nozzle |
| Profile | 0.16 mm High Quality @BBL X1C |
| Layer height | 0.16 mm (0.2 mm first layer) |
| Material | PLA |
| Walls | 2 |
| Infill | 15% |
| Supports | **None** — both parts print flat |
| Brim | Auto |
| Plate | Textured PEI |

Nothing here is X1C-specific. Any 0.4 mm FDM printer running a 0.15–0.2 mm
profile in PLA or PETG will produce usable parts; open the STL rather than the
3MF and use your own profile.

## Assembly

The board sits in the back tray, display upwards, with the USB connector
through the side cut-out. The bezel goes on top and the two plates screw
together through the four corner holes.

**Fastener sizes are not documented here because they were not measured.**
Check the holes against what you have — self-tapping M2 or M2.5 screws are the
usual answer for a shell this size — and if you fit it, please open a pull
request adding the size and length that worked, so the next person does not
have to work it out again.

There is no dedicated battery compartment. The single-cell Li-ion/LiPo pack the
board charges (see the Battery section of the [README](../../README.md)) has to
be placed in the free space in the tray by hand.

## Status

Modelled and sliced for this board; treat the fit as **not yet verified in this
repository**. If you print it, say what happened —
[open an issue](https://github.com/iamankushpandit/Gume/issues) or a pull
request against this file. A note saying "printed, fits, M2×6 screws" is a real
contribution.
