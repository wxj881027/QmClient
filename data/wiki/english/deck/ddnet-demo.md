## What this controls

Three unrelated things on one card: auto-saving your best race demo on race maps, cutting a replay of the last few minutes at any time, and saving your best route as a ghost Tee.

## In game

- Auto recording works only on race servers. It starts writing when you enter the map, so your first finish carries a few seconds of pre-start preparation; if you don't cross the start line within 20 seconds, it stops on its own.
- Recording stops one second after you finish, then the comparison: the demo is kept only if faster than anything the map already has, slower old demos are deleted along with it, and each map ends up with exactly one.
- Dying mid-run stops recording; that demo doesn't count.
- With "Enable replays" on, the ESC menu gains a "Save last 5 min" button, and clicking it cuts the file; without the switch the button doesn't exist.
- The ghost is your own best route, follows you only after you cross the start line, 40% opaque by default.

## How to tune it

| Want | Turn on | Cost |
| --- | --- | --- |
| Review your fastest run | "Save the best demo of each race" | One demo per map, slower runs dropped |
| Keep a recent highlight | "Enable replays" | Writing to disk the whole match |
| Study your own route | "Enable ghost" + "Show ghost" | Only visible after crossing the start |

- "ESC replay minutes" sets how long the button cuts, default 5 minutes.
- "Default length" is the seconds cut when no length is given, default 30, used by the console save command.
- "Opacity" only affects the ghost, default 40% — raise it if you can't see it clearly.

## Watch out

- "Only save improvements" is on by default. Turn it off and every finish adds a ghost file, slow runs included, and the directory keeps growing.
- Auto recording writes a temp file first and decides later; a force-quit or crash can leave temp files behind — clean the demo directory yourself.
- Replays store the whole network flow; size grows with length and player count. Leave it running all day and it costs a day's worth.
- Auto recording and ghosts only recognize race servers; on normal PvP servers nothing records or plays. Short replays don't care about server type — they work everywhere.
- Ghosts are stored per map; switching maps doesn't apply one to the previous route.
- While spectating, nothing records and no ghost plays — run the map yourself.

## Related

- [Replays](deck:general-recording)
- [Gameplay](deck:ddnet-gameplay)
