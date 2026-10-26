## What this controls

Eight common functions as binds, one row each: every row has a key capture on the right — press the key you want and the bind is written for you, so you never type `bind` yourself. The target key's existing bind gets replaced.

## In game

| Row | Command | While held |
|---|---|---|
| HDF | `+toggle cl_dummy_hammer 1 0` | Switch dummy hammer, release to revert |
| DF | `+fire; +toggle cl_dummy_hammer 1 0` | Fire while switching dummy hammer |
| 45° Aim | `echo You are using 45-degree aim; +toggle cl_mouse_max_distance 2 400; +toggle_restore inp_mousesens 1` | Echoes a hint to the console on press, then drops aim distance to 2 and sets sensitivity to 1 |
| Gap aim rescue | `+toggle_restore inp_mousesens 1` | Sensitivity set to 1 |
| Left / Right jump | `+jump; +left` / `+jump; +right` | Jump plus direction in the same frame |
| Weapon Trajectory | `+showweapontrajectory` | Show trajectory while held |
| Active disconnect | `qm_timeout_disconnect` | Silent disconnect on press |

## Watch out

- Commands starting with `+` work while held and revert on release: `+toggle var A B` writes A on press and B on release, `+toggle_restore` restores the value from before the press. On release, 45° Aim writes `cl_mouse_max_distance` back to 400.
- The Weapon Trajectory key only reacts when the display mode is "Show on key"; "Always show" needs no key, and "Off" gives no reaction.
- Active disconnect cuts both main and dummy while keeping the Tee timeout protection.

## Related

- [Bind](qm:bind_editor)
- [Weapon Trajectory](qm:weapon_trajectory)
