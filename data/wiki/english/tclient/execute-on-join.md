## What this controls

Two text fields of commands that run automatically when connecting: "Execute before connecting" runs the moment you press connect; "Execute on connect" runs once after you join the server, once a few snapshots have arrived. It saves you retyping setup commands on every reconnect.

## In game

- Both run in the local console, not as chat messages; the server only sees the results the commands produce.
- Semicolons are separators, so one field can chain several commands.
- "Execute on connect" runs once per connection and a map change does not re-run it; the "Delay" slider converts to roughly one snapshot per 20ms, default 140ms.
- It suits commands that only hold once the server state is in place, such as changing the HUD or sending a join greeting.

## How to tune it

1. Put server-independent local settings in "Execute before connecting": sensitivity, prediction, HUD toggles and the like.
2. For "Execute on connect", try the default delay first; if commands run too early (team info has not arrived yet), add delay, two or three steps at a time.
3. Connect to one server and read the console errors, removing the broken line by line instead of running the whole block with faults in it.
4. Both fields may be left empty; empty means nothing runs.

## Watch out

- After you typed a password manually, or when reconnecting to the same address, "Execute before connecting" is skipped — do not put critical settings only there.
- Each field is a single-line string capped at 100 bytes; a few long commands need to be split into aliases or moved to the other field.

## Related

- [Console](qm:console)
- [Key binds](qm:key_binds)
