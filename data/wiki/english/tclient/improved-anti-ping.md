## What this controls

Another anti-ping smoothing for other players: prediction confidence decides between the predicted and the server position, replacing vanilla time-based blending.

## In game

- Requires vanilla prediction, anti-ping and predict-other-players to be on. While spectating, in replays or paused it takes no part — changing its parameters shows no difference.
- An abrupt predicted-direction change drops confidence sharply and the drawn position falls back to the server position; a stable direction recovers tick by tick and leans back onto prediction. Larger "Uncertainty duration" recovers slower — steadier on screen, further behind.
- "Optimistic prediction along stable direction" leans harder onto prediction while the other player keeps moving one way; off, normal confidence applies.
- The final position is clamped into the bounding box of recent positions, reducing predicted clipping through walls.

## How to tune it

1. Enable only "Use new smoothing algorithm", keep the rest default, play a round and compare with vanilla.
2. If other players jump around, raise "Uncertainty duration"; if they lag half a beat behind, lower it — 25% at a time.
3. On Gores-like maps with frequent bounces where walls keep disproving prediction, also enable "Negative stability buffer (for Gores)" so the conservative state lasts longer.
4. Vanilla smoothing of other players (cl_antiping_smooth) is recommended off: with both running, this algorithm's result overrides it.

## Watch out

- Covers only other players; your own Tee never goes through this algorithm.
- After switching servers your ping differs, so the previous scaling usually no longer fits — try again.

## Related

- [Ghost tools](tclient:ghost-tools)
- [Anti-latency tools](tclient:anti-latency-tools)
