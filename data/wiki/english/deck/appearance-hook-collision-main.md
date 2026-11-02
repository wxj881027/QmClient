## What this controls

A straight line drawn from your Tee along the crosshair direction that predicts what the hook would hit. It stops at anything hookable or not; its length follows the current map's hook length.

## In game

The line color is the answer: the default red means nothing hookable here, green means hookable tile, yellow a player. Checking whether a hold works is one glance, faster than hooking and missing.

Two situations make the line unreliable: crossing a multi-exit teleporter, or computing too long without a result — it then turns into the inverse of yellow. Colors are computed for you standing still; while running and aiming, the real landing point drifts.

## How to tune it

Your line and other players' lines are two independent switches. On, each shows only while the hook key is held by default; tick "Always show ..." right below to keep it on screen.

"Width of your own hook collision line" and "Width of others' hook collision line" default to 0, a one-pixel hairline; at 20 it is about ten pixels and covers tile seams. "Hook collision line opacity" defaults to 100%.

To swap colors while the hook key is held, first turn on "Custom pressed hook line colors"; only then do the four "Pressed: ..." rows below take effect. With it off, holding the key inverts the whole line automatically.

## Watch out

The pressed colors only appear when "Always show" is on and the hook key is held together. In hold-to-show mode, holding the key still shows the normal colors.

"Hook line tip" is the small extra segment at the end of the line at full hook length; fully transparent means it isn't drawn.

When the server disallows hook collision lines, no line appears at all — changing anything here won't help.

## Related

- [Hook collision line preview](deck:appearance-hook-collision-preview)
- [Collision hitbox mode](qm:collision_hitbox)
