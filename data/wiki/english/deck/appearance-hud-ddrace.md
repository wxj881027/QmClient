## What this controls

DDRace-related HUD switches: client IDs, the DDRace body (jumps left and rainbow colors), spectator count, dummy status, position, speed, aim angle, and the unfreeze progress bar.

## In game

With "Show DDRace HUD" on, jump indicators appear at the top left, lit or unlit by the jumps you have left; below are weapon icons you already picked up (current weapon opaque, the rest translucent) and this map's special ability icons. "HUD rainbow colors" cycles colors over time on this block, the key states and player stats; off, they return to fixed text color.

When someone is frozen, a bar appears below them and unfreezes them once it fills.

"Show client IDs (scoreboard, chat, spectating)" puts a number before names in scoreboard, chat and spectator list — handy for calling positions and telling people apart, at the cost of longer lines.

"Show player position", "Show player speed" and "Show player target angle" put three numbers on screen for measuring distances while practicing.

## How to tune it

Turn the master on first, then items as needed. Keep position and speed while practicing; turn them off in serious matches to save screen space.

"Opacity of freeze bars inside freeze" defaults to 0: frozen while standing in freeze tiles, the bar isn't drawn — raise it to 30 to 60. Frozen without standing in freeze tiles, it draws at normal opacity.

## Watch out

The DDRace body needs three things at once: the server declares the block, your Tee carries extended data, and the client switch is on. Regular PvP servers usually meet none, so the block never appears — the setting did save.

The unfreeze bar also needs the server to send extended display info; without it, it never shows.

Position and speed are interpolated snapshots, slightly behind the Tee you see — don't judge jump frames with them.

## Related

- [HUD](deck:appearance-hud-main)
- [DDRace HUD Pro](qm:bind_status_hud)
