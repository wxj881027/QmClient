## What this controls

Draws the path each Tee has walked as a ribbon trailing behind it — a way to see which route a teammate came from, or whether you have been running in circles. Rendered locally only.

## In game

- "Trail length" is how long the tail lives, not how long it looks: stand still and it shrinks to a point; it only spreads out while you move.
- Death, teleports and switching prediction source all clear the trail; changing style or length instantly resets everyone's trails, which vanish for a moment and grow back.
- When you switch to the dummy, the old path is kept, broken into two segments between the old and new positions.
- Other players' trails only draw if the camera allows zooming; for players on other teams, the show-others opacity stacks on top.

## How to tune it

- "Enable tee trails" is the master switch; if you do not want to see others, turn off "Show other tees' trails".
- Color mode, pick one of four: Solid takes its color from "Tee trail color"; Tee uses each player's own skin colors; Rainbow staggers the hue per player over time; Speed maps speed onto blue-to-red, for reading acceleration and braking.
- Style dropdown: Original, "Manga: Ink Slash", "Magic: Spirit Script", "Pixel: Layer Shift"; "Use style colors" switches to the style's own palette.
- "Taper trail width" makes the tail thinner as it goes, "Fade trail alpha" makes the tail end transparent; both together give the best sense of direction.
- "Trail width" 0–20 (default 15), "Trail length" 0–200 (default 25), "Trail alpha" 0–100% (default 80%).

## Watch out

- Returning to the main menu or turning off the master switch clears all trails immediately, nothing left behind.
- On crowded maps, keeping only your own trail is the easiest on the eyes and on the frame rate.

## Related

- [Rainbow](tclient:rainbow)
- [Visual effects](tclient:visual-effects)
