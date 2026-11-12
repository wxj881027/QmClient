## What this controls

What you can see in a match: how far the map's entity layer covers the terrain, whether map text draws, how other teams' tees are drawn, whether background decoration draws, the initial zoom after entering a map — plus how far ahead the client predicts other players.

## In game

- "Overlay entities" at 100 stops drawing normal terrain entirely — only entity tiles and the background remain, and mechanisms are clear at a glance; back to 0 restores the regular map.
- With "Show others" on, other teams' tees become translucent, 40% opacity by default; check "Show others (own team only)" and other teams are not drawn at all.
- Turning off "Show background quads" skips the map's decorative quads — a cleaner picture and one less layer of cost.
- With "AntiPing (latency compensation)" on, other players' movement and shots draw as predicted; at high latency it feels more responsive, but the drawn positions are guesses.
- After changing "Default zoom" you return to the game in the new view at once; the wheel or keys can still change it later.

## How to tune it

| Goal | Which control |
| --- | --- |
| See mechanisms clearly | Push "Overlay entities" toward 100 |
| See terrain | "Overlay entities" back to 0 |
| Less decoration | Turn off "Show background quads" |
| Others smooth at high latency | Turn on AntiPing, raise the prediction margin |
| Map text too small | "Show text entities" + "Size" |

Prediction margin starts at 10; raise it when stutter shows, and past the point where the feel goes floaty you have gone too far.

## Watch out

- "Show others" must be sent to the server; with showothers disabled server-side, no local change does anything.
- Prediction margin defaults to 10 ms. When the server doesn't sync weapon input it is locked to 10, and card changes do nothing; with "Auto margin" on it is only a floor, and the actual value runs higher.
- AntiPing turns off automatically in spectate and replays, so predicted positions in a match not matching real positions in a replay is normal.
- "AntiPing: predict other players" only writes 0 and 1. If your config already holds 2 or 3 (set via console), clicking it flips between 2 and 3 and never turns off.
- At 100 the base color also swaps to the entities background color — it looks like a different map, not a rendering bug.
- "Predict events (experimental)" is off by default. On, effects like empty jumps play at predicted times, and on unstable networks they may drift slightly from reality.

## Related

- [Background](deck:ddnet-background)
- [Entity overlay](qm:entity_overlay)
