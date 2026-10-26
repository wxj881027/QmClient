## What this controls

A radial menu that opens at screen center while the bind key is held; the item under the stopped pointer executes on release. Targets are picked with the crosshair: using the crosshair's world position (camera center while spectating), the menu finds the nearest player within "Detection distance" (100–2000, default 400); your own tee, your dummy and spectators do not count.

## In game

- Eleven options form sectors according to their toggles, and disabled ones take no slot: Friend, Whisper, Mention, Copy skin, Swap, Spectate, Invite to team, Join team, Follow server, View points, Copy name; plus a ring of its own for the "Rename queue".
- With no player near the crosshair, it shows "No player nearby"; if a rename queue is configured, the menu still opens, so you can rename yourself.
- After picking "Follow server", the client auto-connects to the server the target is on and retries at intervals if the target drops; the pie menu card then shows "Following: name" and a stop button.
- While chat or console is open, the key does not open the menu.

## How to tune it

1. Tick "Enable pie menu", then give the "Pie Menu" bind (`+pie_menu`, hold to open, release to execute) an unused key in the "Bind" card.
2. Keep three to five common options; fewer sectors make hits steadier. Each option's color swatch on the right tunes its own color, and "Pie menu highlight tint" controls the hover highlight.
3. "UI scale" (50–200%) and "Opacity" tune the look, "Pie menu effects" handles the animation; if it gets messy, use "Reset pie menu appearance".
4. Keep "Detection distance" small for close-range actions and larger for finding players across teams; while spectating it does not apply — the nearest player on the whole server is picked, which may not be the one on camera.
5. In "Rename queue", separate multiple names with `|`; the `mainname,dummyname` form renames both connections at once; an incomplete two-name entry never executes.

## Watch out

- The menu always opens at screen center, not next to your player; a crosshair drifting far away selects nobody.
- Its bind shares a key slot with other commands; confirm the key is free before configuring.
- Invite to team and Join team only make sense on maps with the team mechanic; they are refused when the conditions are not met.

## Related

- [Bind](qm:bind_editor)
- [Bind wheel editor](deck:tclient-bind-wheel-editor)
