## What this controls

Player and dummy each have a skin queue that rotates on a fixed interval. Entries are skin name + optional colors; you can reorder and shuffle, and save queues as presets.

## In game

With "Enable rotation" on, every switch interval advances to the next entry and your on-screen look follows. The interval counts in milliseconds, default 600 — nearly two switches per second.

Each switch reports the new look to the server at once. But the server accepts one change per 5 seconds by default and drops the packets in between, so others see you change at most every 5 seconds, however fast your own screen flips.

Rotation only runs while connected: the player queue checks online status, the dummy queue checks dummy connection. While disconnected it stops and resumes there after reconnect; with "Random skin on map join" on, it instead starts at a random entry.

## How to tune it

1. Decide who it is for first: the "Player" and "Dummy" buttons above; the two queues are independent.
2. Turn on "Enable rotation" and set "Switch interval" to a pace you accept. 600 is twitchy; 30000 to 60000 suits running maps. Setting 0 stops timed switching.
3. Queue skins from the skin browser above with the plus icon, or load a ready-made set from the "Preset bar".
4. For a different opener on every map, tick "Random skin on map join": each rotation starts from a random entry.

| Control | Effect |
| --- | --- |
| Dice | Applies a random queue entry immediately |
| Arrows | Shuffles the order, content unchanged |
| Trash can (queue head) | Clears the whole queue |
| Click an entry (no drag) | Switches to it immediately |
| Drag an entry | Reorders; applies on release |
| Trash can at the entry's end | Removes just this entry |

## Watch out

The queue cap is 20 by default; the plus past the cap gives no message and no error. One skin with one color can enter only once.

Applying a preset replaces the current queue wholesale; manual edits are not saved back automatically — use save-to-preset to keep them, or save as a new preset.

"Server preset" is a read-only template: once applied, the queue is rebuilt every frame from the skins other players in the room wear, in their order, and your manual entries get overwritten. The first two built-in presets cannot be deleted.

A tiny interval (say 1 ms) only makes your client switch and resend look info every frame; others still see the server's 5-second cooldown — bandwidth wasted.

## Related

- [Skin browser](deck:tee-skin-list)
- [Skin options](deck:tee-skin-options)
