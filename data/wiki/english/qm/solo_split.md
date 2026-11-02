## What this controls

One-key team split: press a key once and the client sends a /team command each for main and dummy, sending the two Tees into two different empty teams.

## In game

Before joining, the client scans the current player list and picks two free team numbers that are neither 0 nor reserved teams. Once the teams are in place, each Tee has its own team, so the map's doors and freeze no longer affect each other. Press the key again (or the leave key) and both connections return to "Team when leaving solo split", team 0 public by default.

## How to tune it

1. Pick "Team when leaving solo split" (0–63, default 0).
2. For one-key convenience tick "Auto-connect dummy": with no dummy connected, pressing the key connects first and then continues the split automatically.
3. Bind "Solo split key" as the master switch; binding "Solo split enter key" and "Solo split leave key" separately guards against mispresses.

## Watch out

- After sending /team the result is not confirmed instantly: the client re-sends at most twice at 2-second intervals and stops once the teams are in place; if they never settle, both Tees are sent back to the teams they had before joining, so you are never left halfway.
- Auto-connecting the dummy has a 10-second window; past it, the operation is void. The leave direction never auto-connects the dummy.
- When no two free teams can be found, the key does nothing at all — normal when the room or the teams are full.
- "Auto team lock" in this card is shared with the HJ assist card: after joining, it locks the team of the Tee you control once the lock delay has passed; the dummy's side is not locked.

## Related

- [Dummy](deck:controls-dummy)
- [Dummy mini view](qm:dummy_miniview)
