## What this controls

Adds X and Y coordinates to name plates (in tiles, two decimals, like `X:12.34`), plus an "X alignment hint with me" highlight. It only changes name plates on your screen; nothing is synced to other players.

## In game

- "Show own coordinates" and "Show other players' coordinates" toggle separately, and X and Y each have their own switch; only X is on by default.
- With "X alignment hint with me" on, a teammate's X is compared against your own tee or dummy, rounded to 0.01 tiles: in non-strict mode, within 0.03 tiles counts as aligned; in strict mode, every digit must be equal. Once alignment holds for the full "Detection time" (100–3000 ms, default 1000), the X number on the name plate turns into the "X alignment color".
- If the gap exceeds the range mid-count, or the reference tee or dummy changes, the timer restarts; players on other teams never take part.
- While the hint is on, X is drawn even if "Show X" is off. Own tee and dummy are compared with each other too — in the dummy view, when you and your dummy align, both name plates light up.

## How to tune it

1. Start with "Show own coordinates" only; enough for noting spots and finding horizontal lines.
2. Add "X alignment hint with me" for two-player sync practice; use strict mode for mechanisms that need the exact same tile, non-strict is more forgiving for ordinary alignment.
3. Set "Detection time" to the rhythm of the two of you: too short and the color flickers, too long and you must stand aligned a long time before it lights up.

## Watch out

- The numbers come from render positions, affected by prediction and smoothing, and may differ from server coordinates by a few tenths of a tile.
- Alignment colors only after you stand aligned for the full detection time; passing by does not trigger it. In strict mode, even 0.01 tiles off stays dark.

## Related

- [Name plate settings](deck:appearance-name-plate-settings)
- [Position jump hint](qm:jump_hint)
