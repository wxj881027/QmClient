## What this controls

A pair of colors for each of the five laser types: an outline color and an inner color, ten rows in total. It only changes how your machine draws them; on other screens they keep their own colors.

## In game

The beam is built from two stacked layers: the outline outside, the core inside, drawn over the outline. That makes the inner color the more visible one; when the two colors are close, the whole beam reads as one solid bar with no separation.

Lasers of other teams are also multiplied by the opacity of "Show others". Once other players are faded, the colors you set fade along with them.

## How to tune it

| Rows | What they cover |
| --- | --- |
| Rifle Laser Outline/Inner Color | The rifle beam, and the fallback for every laser without its own colors |
| Shotgun Laser Outline/Inner Color | The beams fired by the shotgun |
| Door Laser Outline/Inner Color | Laser door beams on the map |
| Freeze Laser Outline/Inner Color | Freeze doors, freeze lasers, freeze guns |
| Dragger Outline/Inner Color | The dragger that pulls players away |

Gun lasers have no colors of their own: freeze-type guns use the "Freeze Laser" pair, the rest fall back to the rifle pair. So tune the rifle first — one place covers the most cases.

"Set all to Rifle" copies the rifle pair into the other four pairs, eight values at once. "Reset to defaults" writes all ten rows back to their initial values; there is no per-row undo.

## Watch out

Changing only the outline or only the inner color easily produces one smeared solid bar.

Demo playback draws lasers with the configuration of the machine playing them back, not the colors at recording time.

## Related

- [Laser effect enhancement](deck:appearance-laser-enhanced)
- [Laser preview](deck:appearance-laser-preview)
