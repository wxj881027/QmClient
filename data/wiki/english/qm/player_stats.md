## What this controls

Three lines of local stats in the mobile info panel — survival time, rescued / water-drop counts, left-right hook ratio — plus a map progress bar. Data accumulates only on your machine, sends no packets, never affects gameplay decisions.

## In game

The survival line shows average and longest survival — time from being unfrozen to entering freeze again, not how long your current life has lasted. Being unfrozen while barely moving counts as a rescue; entering freeze settles one water drop. Hooks are counted by direction, left and right. Map progress is a percentage estimated from your position to the finish, showing `--` right after joining until the estimate exists. While spectating with no visible character, the three lines are hidden.

## How to tune it

| Item | What it does | Trade-off |
| --- | --- | --- |
| Show player stats HUD | Master switch for the three text lines | Turning it off also removes the embedded bar |
| Map progress bar | Standalone bar; width, height, color and position adjustable | Positioned by screen percentage |
| Use embedded HUD progress bar | Puts the progress inside the stats area | Saves space; needs the stats HUD on |
| Show dotted map route debug | Draws the test-point route used by the progress calculation | Only for tuning the progress display |
| Reset stats when joining a server | Clears on join | Turn it off to accumulate across maps and days |

## Watch out

- Stats do not reset by default: changing maps or servers keeps accumulating. Enable reset-on-join for per-map numbers.
- Self and dummy each keep their own record; the HUD shows the one you currently control.
- Progress supports Gores mode, NUT_race9 and DDRace maps with a race marker; other maps have no progress to compute.
- The standalone bar does not depend on the stats HUD: it stays even with the three lines off.

## Related

- [Gores mode](qm:gores)
- [HUD settings](tclient:hud)
