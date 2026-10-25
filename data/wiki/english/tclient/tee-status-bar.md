## What this controls

The team freeze panel at the top of the screen: a small Tee icon for each teammate, plus a counter line reading alive/total or frozen/total. It only counts players on your team, is drawn locally, and teammates cannot see it.

## In game

- Frozen players are drawn as the ninja by default; with "Use custom skins instead of the ninja tee" on, their own skin is drawn instead — darkened, semi-transparent, with a pain expression.
- While spectating, the count follows the team of the player you are watching.
- The whole block hides while the scoreboard is open; it only works on servers with DDRace entities.

## How to tune it

- "Show tee status bar" is the master switch (on by default); "Only show after joining a team" keeps the whole server from being drawn as one long row when you are not in a team.
- "Maximum rows" is 1–6; how many fit per row is derived from the screen aspect ratio and "Tee size". When space runs out, moving players are drawn first and frozen ones fill in the rest — so when it is full, the frozen ones disappear first.
- The counter is two separate toggles: "Show the number of tees still alive" shows how many can still move, "Show the number of frozen tees" shows frozen/total.

## Watch out

- When icons go missing, first suspect the row capacity, not a player leaving.
- The icons are a hint only; freeze state is decided by the game.

## Related

- [Status bar settings](deck:tclient-status-bar-settings)
- [HUD](deck:appearance-hud-main)
