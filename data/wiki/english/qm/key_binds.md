## What this controls

Eight common actions as one-click binds: every row has a key capture on the right — press the key you want and the bind is written for you, so you never type `bind` yourself. The target key's existing bind gets replaced.

## The eight rows

| Row | Command | While held |
|---|---|---|
| HDF | `+toggle cl_dummy_hammer 1 0` | Switch dummy hammer, revert on release |
| DF | `+fire; +toggle cl_dummy_hammer 1 0` | Fire while switching dummy hammer |
| 45° Aim | `echo You are using 45-degree aim; +toggle cl_mouse_max_distance 2 400; +toggle_restore inp_mousesens 1` | Echoes a hint, drops aim distance to 2, sets sensitivity to 1 temporarily |
| Gap aim rescue | `+toggle_restore inp_mousesens 1` | Sensitivity set to 1, restored on release |
| Left / Right jump | `+jump; +left` / `+jump; +right` | Jump plus direction in the same frame |
| Weapon Trajectory | `+showweapontrajectory` | Show trajectory while held |
| Active disconnect | `qm_timeout_disconnect` | Silent disconnect on press |

## Why HDF uses +toggle, not toggle

`toggle` flips on press and stays until the next press, so an interrupted sequence leaves it on. `+toggle` returns to off the moment you let go — mistakes cost less.

The official deep fly tutorial uses the same idea: `bind mouse1 "+fire; +toggle cl_dummy_hammer 1 0"`. It also warns that the **off side must reset `cl_dummy_hammer`**, otherwise the dummy gets stuck hammering on its own. The command this card writes already avoids that trap.

## 45° Aim: put your own values in

The official example hard-codes the sensitivity on the way back (for instance `+toggle inp_mousesens 1 200`). If yours is not 200, one press of 45° Aim silently rewrites your sensitivity.

This card uses `+toggle_restore` instead: on release it restores **whatever was there before**, no matter the number. `cl_mouse_max_distance` stays fixed at 2/400, because it defines the 45° geometry rather than your feel.

To use your own numbers, type `cl_mouse_max_distance` and `inp_mousesens` in the console first, then put those values into the command.

## Watch out

- The Weapon Trajectory key only reacts when the display mode is "Show on key"; "Always show" needs no key, and "Off" gives no reaction.
- Active disconnect cuts both main and dummy, keeping the Tee timeout protection.
- All eight rows write bare keys, never modifier slots; use the "Bind" card for combos.
- After writing, search in "Bind overview" to confirm you did not overwrite a key you rely on.

## Further reading

- DDNet Wiki · Binds: the original `+toggle` and deep fly write-up, including the stuck-hammer pitfall

## Related

- [Bind](qm:bind_editor)
- [Bind overview](qm:bind_overview)
- [Weapon Trajectory](qm:weapon_trajectory)
