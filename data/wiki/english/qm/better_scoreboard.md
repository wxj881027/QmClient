## What this controls

Adds a column to the vanilla scoreboard (points or accumulated playtime), opens the scoreboard automatically after death, adds wheel paging on crowded servers, and filters by name or clan. The five sub-settings take effect independently.

## In game

- With "Scoreboard point check" ticked, the scoreboard gains a value column: opening it queries the points API player by player, at most two requests at a time, and results are written to the SQLite cache — so the column is often empty at first glance and has numbers the second time you open it.
- The header button "Show playtime / Show points" switches that column between accumulated playtime hours and points.
- "Show scoreboard after death" pops the scoreboard up automatically when you die and are not spectating.
- "Scroll mode" only applies in non-team gamemodes with more than 16 players: a fixed single column of 16 rows, wheel to page; the scoreboard itself also has a toggle to switch it directly.
- "Scoreboard filter: only show players whose name or clan contains this text" — once filled, only players whose name or clan contains that text (case-insensitive) stay in the list.

## How to tune it

- To rank by points: tick "Scoreboard point check", then click the header sort button to switch between score/time and points; players whose points were not fetched fall to the back of the list and the order shifts.
- To see how long the players on screen have played in total: tick "Scoreboard playtime", then switch the header to playtime. It is off by default; when enabled it takes priority over points but does not change sorting.
- "Scroll mode" is only needed for practising on crowded servers; in team mode it has no effect.

## Watch out

- The master switch "Better scoreboard" currently controls nothing: with it off, the five sub-settings still apply — turn them off one by one.
- Playtime adds up `activity[].hours_played` from the official JSON, shown in hours; it does not use the last-year figure or client-side time. It shares the same request as points and is only cached for the current session, never written into the points database. An empty array shows 0, loading shows `...`, and a missing field, no valid hours, overflow or a failed request shows `?`; if a refresh fails, previously loaded numbers stay visible.
- On Windows, points and playtime lookups follow the system proxy automatically, including manual proxies, PAC and bypass rules; existing environment-variable proxies keep their priority. This only governs the HTTP requests for player points and playtime lookups, and proxy resolution runs in the background — if it takes longer than 8 seconds the request goes out over the original network path.
- The filter matches both name and clan; when the scoreboard suddenly shows only a few players, clear the filter box first before investigating further.
- The filter text is not saved and is cleared after restarting the client.

## Related

- [Player statistics](qm:player_stats)
- [DDRace HUD](deck:appearance-hud-ddrace)
