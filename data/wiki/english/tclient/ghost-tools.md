## What this controls

Extra translucent ghosts at other players' real snapshot positions, drawn beside the predicted position to judge hook and shot timing.

## In game

- Without "Swap ghosts with regular players", the body is drawn at the predicted position, the ghost at the snapshot position; with it, the two swap — body on the snapshot position, ghost on the predicted one.
- The body copy uses "Predicted ghost opacity" (default 100%), the ghost copy uses "Unpredicted ghost opacity" (default 50%). Swapping only exchanges positions; the sliders keep their assignments.
- Ghosts only have a position — no weapons, no particles; server decisions and other screens are unaffected.
- Drawn only while online and not spectating — never in replays or spectator mode, so the option "doing nothing" there is normal.

## How to tune it

1. Turn on only the main toggle first; the default 100/50 already separates the two copies.
2. If crowds look too busy, fade the unpredicted ghost to 30–40 or tick "Render ghosts as circles".
3. Bind "Toggle ghost key" to switch ghosts temporarily without returning to the card.
4. "Hide ghosts of frozen players" removes frozen players' ghosts entirely while swapping is off; with swapping on, their ghosts are still drawn and do not fade.

## Watch out

- A target off screen keeps being drawn only while swapping is on.
- With swapping on, body and ghost sitting close together is normal: one is predicted, one at the server position.

## Related

- [Improved anti-ping](tclient:improved-anti-ping)
- [Anti-latency tools](tclient:anti-latency-tools)
