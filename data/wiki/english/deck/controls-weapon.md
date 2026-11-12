## What this controls

Eight weapon keys: Hammer, Pistol, Shotgun, Grenade, Laser, "Prev. weapon", "Next weapon", and the weapon trajectory line.

## In game

A direct key only sends "I want this one" once per press. The server checks first: without that gun the request is dropped and nothing switches.

"Prev. weapon" and "Next weapon" count presses and walk the slot order, skipping guns you don't have, so one press can jump past two.

The switch itself waits for the current action to finish. During the recovery right after a shot, switching is postponed, and firing in that window is swallowed — never fired with the old gun.

With the ninja in hand, all switch requests are ignored.

## How to tune it

| Style | Feel | Suggestion |
| --- | --- | --- |
| Direct keys | One step | Bind the guns you use often |
| Prev. / Next | Several presses | Keep as fallback |

"Weapon Trajectory" draws only while held and vanishes on release; it confirms angles and never changes the bullet.

## Watch out

One key holds one command — binding a weapon squeezes out whatever action used that key.

With only a cycle key and no direct keys, a rushed switch takes many presses and costs a beat.

## Related

- [Movement](deck:controls-movement)
- [Weapon trajectory](qm:weapon_trajectory)
