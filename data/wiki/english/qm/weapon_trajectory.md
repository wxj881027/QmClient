## What this controls

Trajectory prediction lines: using map collision and the current tuning, it rehearses how projectiles will fly and draws the path in the scene. It only draws — it never changes the projectiles.

## In game

Grenades get a fading arc with a small grenade cursor at the landing point; pistol and laser draw straight lines, with the laser bending per its range and bounce limits; with "Predict ninja path" on, the local player's ninja dash endpoint shows a ghost. A line stops when it hits a player or a wall; if the hit is one you could actually land (including tele-gun walls), the whole line inverts its color and is unmistakable.

## How to tune it

1. "Display mode" defaults to "Show on key". The bind does not live in this card: go to the "Key Bindings" card and bind the "Weapon Trajectory" row to a key — the line draws only while held.
2. Do not want a key? Pick "Always show"; "Pistol guide line" controls the pistol's line separately.
3. "Guide line color" only affects grenades: the pistol is fixed white, and shotgun and laser follow their own laser inner-ring colors.
4. If the lines are too loud, lower "Opacity" (default 70%) first, then touch "Line width".

## Watch out

- Lines are drawn for every player on screen holding these weapons, opponents included, not just you.
- "Show on key" only accepts that one bind from the "Key Bindings" card (command `+showweapontrajectory`); with no key bound it does nothing at all — switch to "Always show" when troubleshooting.
- When focus mode is set to hide guide lines, nothing is drawn; in Gores, "Hide guide lines" overrides "Always show", though holding the key still shows them temporarily.

## Related

- [Weapon animation](qm:weapon_animation)
- [Weapon](deck:controls-weapon)
