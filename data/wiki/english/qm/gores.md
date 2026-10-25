## What this controls

A set of automations for Gores / KOG maps: with the master switch on, the client swaps your weapon to the hammer on every click and switches back after the hammer lands, plus Axiom community auto login. It rewrites the input you send, so both the on-screen prediction and the packets actually sent are the rewritten ones.

## In game

- On the frame you press fire, the weapon switches to the hammer and switches back to your previous gun after the hit — no number keys needed.
- The instant someone else's hammer wakes you, one extra hammer is added, then released for you — no repeated hammering.
- After picking up shotgun, grenade, laser or ninja, "Disable after picking up other weapons" makes the cycle exit and stops grabbing your weapon.
- Toggling the master switch echoes "Gores Mode: On/Off".

## How to tune it

For permanent KOG play, turn on "Auto enable in Gores mode": it opens on entering a Gores server and restores your original settings on leaving. For occasional play, use "Enable Gores mode" and bind "Gores mode key".

| Option | Effect |
| --- | --- |
| Auto weapon switch | The hammer-gun cycle itself |
| Auto-toggle fast input / others | Fast input on while in Gores, restored on exit; both entries are taken over meanwhile |
| Temporarily disable dummy hammering | Dummy hammering off once at entry, then untouched |
| Hide guide lines | Hides trajectory guide lines and the like inside Gores |
| Skip switch animation when hammering | No switch animation while cycling between hammer and gun |

## Watch out

Do not keep it on in regular servers: fire becomes hammer and the whole rhythm changes.

Axiom auto login is a separate switch: with "Axiom main account password" and "Axiom dummy password" filled in, joining an Axiom community server sends /login automatically. Passwords are stored in plain text in the local config — do not fill them in on shared computers.

## Related

- [Gores drown board](qm:gores_drown_board)
- [Gores actor](qm:gores_actor)
