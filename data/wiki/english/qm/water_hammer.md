## What this controls

Teammate state highlighting: teammates on your team who are frozen while holding the hammer with fire pressed are drawn in orange-red, so you can find who needs a wake-up hammer in a messy fight. It only changes the rendering on your own screen.

## In game

The color applies when a teammate is frozen, holds the hammer (or is switching to it) and has fire held — all three at once; releasing fire or unfreezing restores them at once. Nobody else sees this color, and it cannot serve as a team alert.

## Watch out

- Your dummy runs on your own local input and can always be judged; other players rely on server-broadcast pre-input, and when the server sends none the highlight never lights up — not a broken setting.
- Team filtering only compares team numbers; on servers where everyone counts as one team it does not tell friend from foe.
- The check ignores position: a frozen teammate anywhere counts, not only in water.

## Related

- [Entity overlay](qm:entity_overlay)
- [HJ assist](qm:hj_assist)
