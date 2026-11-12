## What this controls

Direction markers around your own Tee: each teammate gets a dot or mini Tee in their direction, visible even off screen. Purely local rendering.

## In game

- Each marker is offset from your Tee toward the other player; it shows direction only, never distance.
- Color follows the other player's state in three tiers — alive, frozen, thawing — mapped to "Indicator alive color", "Indicator in freeze color" and "Indicator safe color".
- Drawn only on race-type servers, not in replays, and only when the camera allows zoom; while spectating there is no local Tee and nothing is drawn.

## How to tune it

- "Show any enabled Indicators" is the master switch; "Only show after joining a team" stops server-wide markers while unteamed — in team 0 everyone counts as same team.
- Markers piling up? Tick "Change indicator offset based on distance to other tees", set the min/max offsets and "Indicator max distance" (500–7000); farther targets get farther-out markers.
- "Hide indicator for tees on your screen" keeps only off-screen markers — useful when looking for stragglers.
- "Render tiny tees instead of circles" draws the other player's skin as a tiny Tee; frozen players' tiny Tees are darkened and the color only affects opacity.
- "Use warlist groups for indicator" switches to the warlist: players outside ticked groups are not drawn at all, an empty list means everything disappears; with "Use warlist colors instead of regular colors" off, colors still follow state.
- "Indicator size" is 1–16 (default 4), "Indicator opacity" 0–100% (default 50%).

## Watch out

- Drawn locally only; nobody sees your markers.
- If markers vanish after enabling warlist mode, first check the list has anyone in it.

## Related

- [War list](deck:tclient-warlist)
- [Visual nameplates](tclient:visual-nameplates)
