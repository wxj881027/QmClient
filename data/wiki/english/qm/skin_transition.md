## What this controls

The transition animation at the moment a skin changes: old and new skins blend in whichever way you pick. There is also "Hammer skin steal" — hammer someone and you copy their skin.

## In game

Changing maps, changing skins or editing Tee colors each trigger the animation: the old look gives way to the new one as an afterimage pop, a soft fade, a slide-left cut and so on. A color edit counts as a skin change and plays it too. It runs only in live games, never in demo playback. Hammer skin steal attaches to a confirmed hammer hit: whoever you hammer has every body part's skin name and colors written into your skin config, the server is told you changed your skin, and others can see it.

## How to tune it

| Item | Content |
| --- | --- |
| Skin transition type | Afterimage pop / Smooth fade / Slide left / Spin pop / Brightness shift / Glitch / Elastic |
| Animation range | Self only / Local (self plus dummy) / All players |
| Skin transition duration | 0–2000 ms; 0 disables the animation |
| Skin transition easing | Ease out cubic / Elastic back / Linear / Ease in out quad |
| Skin transition intensity | 0–300%, sets the animation amplitude |

Hammer skin steal is a separate switch and works even with the animation off.

## Watch out

- The range has no "others only" option — only Self only, Local and All players.
- On crowded servers skins change frequently and the animations run back to back; start with a short duration.
- Hammer skin steal edits your real skin config; turn it on during serious runs or matches and your skin really becomes theirs.

## Related

- [Tee appearance](qm:skin_appearance)
- [Tee skin options](deck:tee-skin-options)
