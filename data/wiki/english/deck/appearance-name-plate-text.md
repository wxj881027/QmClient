## What this controls

Text effects for nameplates: border, gradient, rainbow, glow. All four can be stacked, and each of three scenes — playing, spectating, replay — decides separately who gets them.

## In game

Border outlines the glyphs so they stay readable over map textures; glow wraps the text in a glowing halo, most visible on dark backgrounds; gradient pushes the name from its original color toward one you pick; rainbow rotates the text color on its own, one cycle in roughly seven seconds.

None of this touches gameplay; the cost is draw calls: border paints the text in eight directions, glow in four, and the name body always paints — one border ring costs twice as much as glow.

When many effect nameplates share the screen, "Automatic effect LOD when crowded" thins the budget by player count: glow's outer rings drop first, then border's; the innermost ring always stays.

## How to tune it

Find the switches under "Show advanced options" first, then enable one at a time. Border is the cheapest and most practical; glow pays off most on dark maps.

"Border range" takes 1 to 4 rings, "Glow range" 1 to 12, but glow only draws up to 6 — beyond that nothing gets brighter. On crowded servers set "Full quality nameplate count" low; 8 to 20 saves a lot.

| Scene | Takes over | Options |
| --- | --- | --- |
| Playing effects | Normal game | Off / Self only / Others only / Friends only / Self and friends / All players |
| Spectate effects | Spectating, free view included | Off / Spectated player / Others only / Friends only / Spectated player and friends / All players |
| Demo effects | Playing a replay | Off / Smart / Manual target / Manual scope |

"Smart" puts effects on the one followed player only; "Manual target" picks a player by client ID; "Manual scope" reuses the playing-effects level above.

## Watch out

All four at once adds up the draw count per nameplate — the fastest way to drop frames on a full screen.

"Playing effects" defaults to all players; narrow the scope before switching effects off entirely.

The preview on the settings page ignores these three scopes and always draws whatever you ticked.

At "Border range" 1 no outer ring is added; the text outline simply takes the "Border color".

## Related

- [Name Plate](deck:appearance-name-plate-settings)
- [Preview](deck:appearance-name-plate-preview)
