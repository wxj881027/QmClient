## What this controls

Adds a points column to the vanilla scoreboard, opens it automatically after death, adds wheel paging when there are many players, and filtering by name or clan. The four sub-settings take effect independently.

## In game

- With "Scoreboard point check" ticked, the scoreboard gains a points column: opening it queries the points API player by player, at most two requests at a time, and results are stored in a local cache — so the column is often empty at first glance and has numbers the second time you open it.
- "Show scoreboard after death" pops the scoreboard up automatically when you die and are not spectating.
- "Scroll mode" only applies in non-team gamemodes with more than 16 players: a fixed single column of 16 rows, wheel to page; the scoreboard itself also has a toggle to switch it directly.
- "Scoreboard filter: only show players whose name or clan contains this text" — once filled, only players whose name or clan contains that text (case-insensitive) stay in the list.

## How to tune it

- To rank by points: tick "Scoreboard point check", then click the header sort button to switch between score/time and points; players whose points were not fetched fall to the back of the list and the order shifts.
- "Scroll mode" is only needed for practising on crowded servers; in team mode it has no effect.

## Watch out

- The master switch "Better scoreboard" currently controls nothing: with it off, the four sub-settings still apply — turn them off one by one.
- The filter matches both name and clan; when the scoreboard suddenly shows only a few players, clear the filter box first before investigating further.
- The filter text is not saved and is cleared after restarting the client.

## Related

- [Player statistics](qm:player_stats)
- [DDRace HUD](deck:appearance-hud-ddrace)
