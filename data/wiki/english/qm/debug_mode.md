## What this controls

A collection of performance diagnostics switches: main thread and render stage timing logs, a dedicated log file, startup stutter diagnostics, plus a millisecond threshold. It only records; it does not change graphics, network or gameplay settings.

## In game

- With any one of the three switches on, the client starts writing performance logs: each frame, stages exceeding the threshold are written as one entry, with at most one thousand detail entries per second — beyond that only a count is kept and reported later.
- With "Enable client stutter diagnostics at startup" on, every client component's update and render is timed separately, so a stall shows which part grew.
- Logs are written to `dumps/QmClient_Perf/qm_perf_<timestamp>.log` in the storage directory; each time you enable it a new file is created, old ones are not overwritten.

## How to tune it

1. "Debug mode" on the card is the unified entry: one click turns all three switches on or off together; tick sub-items individually if you want only half of them.
2. "Performance debug log threshold (ms)" defaults to 4, which is enough for ordinary problem reports; raise it if you only want to catch severe freezes.
3. Reproduce the stall once (enter the map, move, fire — replay what you did), then hand over the logs under `dumps/QmClient_Perf` together with the reproduction steps, and turn the switches off again.

## Watch out

- While stutter diagnostics is on, the effective threshold is forced down to about 3.3 ms; raising the slider then has no effect, while lowering it still works.
- With the threshold set too low, normal fluctuation gets written into the log and the real scene is easier to bury.
- Component-level timing has its own overhead; leaving it on for a long time pollutes the data you want to measure.

## Related

- [Debug graphs](qm:debug_graph)
- [Console](qm:console)
