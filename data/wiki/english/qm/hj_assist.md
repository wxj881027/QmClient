## What this controls

Unfreeze-moment automations: when someone else hammers you awake, it can leave spectator mode for you, switch to the unfrozen Tee, close the chat box, or pop up a notification. It only touches input and UI and never changes freeze detection.

## In game

The trigger only accepts "someone else's hammer": frozen last frame, unfrozen this frame, and the hammerer is neither you nor your dummy. Unfreezing on your own triggers no action.

## How to tune it

| Toggle | Action |
| --- | --- |
| Auto unspec on unfreeze | When the Tee you control is hammered awake, sends /spec for it to leave spectator mode |
| Auto unspec both tees when either is hammered awake | Either main or dummy getting hammered awake pulls both connections out of spectator mode |
| Auto switch to the tee that got unfrozen | With both frozen, when the other is hammered awake while yours still is, control switches to the unfrozen Tee |
| Automatically close the current chat after waking from freeze | With the chat box open, the draft is saved first, then it closes; "Save unsent chat draft" is on by default, so you can continue next time |
| Show wake-up popup on the other tee | A popup appears next to the other Tee, randomly on the left or right, gone after two seconds |
| Auto team lock | After joining a lockable team, sends /lock 1 once "Lock delay" (default 5 seconds) has passed; only the team of the Tee you control is locked |

The card also has "Fade spectators in water": when a spectator ghost Tee falls into a death or freeze tile, it drops to "Spectator opacity" (default 40%), so living players are easier to see in water.

## Watch out

- Turn on one item at a time. With everything on, it is hard to tell which action interrupted you.
- Auto switching changes which Tee you control; while playing the dummy, this is easy to misread as stuck input.
- Lock delay 0 locks the team the moment you join, leaving teammates no time to join; too long and the lock may land only after the situation has changed.

## Related

- [Water hammer highlight](qm:water_hammer)
- [Solo split](qm:solo_split)
