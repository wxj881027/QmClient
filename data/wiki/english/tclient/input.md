## What this controls

Input assists that shorten the gap between pressing a key and seeing the result: fast input, auto margin and Sub-Tick aiming. They change the timing of local sending and prediction only; server decisions stay the same.

## In game

- "Fast input (reduce visual latency)" makes your input take effect "Amount" milliseconds early: the predicted world is pushed a few more ticks ahead, so a key press shows up on screen sooner. On the frame you press hook or fire, the mouse direction is pinned into this early input, so that flick stays accurate.
- The lead also raises the lower bound of the prediction margin (the larger of "Base prediction margin" and the lead), so the more lead you add, the further behind other players' positions look.
- "Auto margin" computes a 1–300ms margin from real-time prediction time, average delay, jitter and connection state; with it off, the fixed lower bound above is used.
- "Sub-Tick aiming" grabs the mouse position on the frame a bind that is exactly +fire / +hook is pressed, uses it as the aim direction for that tick, then clears it.

## How to tune it

| Symptom | Try first | Note |
| --- | --- | --- |
| Keys feel half a beat late | Enable fast input | Default 20ms to start, step up notch by notch |
| Pull-back after adding lead | Take the lead back to 20–40ms | The more lead, the easier it pulls back |
| Unstable at high latency | Enable auto margin | Off means back to the fixed lower bound |
| Hammer flicks / taps drift off | Enable Sub-Tick aiming | Only direct presses count; script triggers do not |

## Watch out

- Too much lead makes other players' positions lag more, with pull-backs and teleports.
- "Fast input others" draws opponents early too; it does not speed you up.
- Sub-Tick aiming captures only the mouse position on the frame the key goes down.

## Related

- [Anti-latency tools](tclient:anti-latency-tools)
- [Mouse settings](deck:controls-mouse)
