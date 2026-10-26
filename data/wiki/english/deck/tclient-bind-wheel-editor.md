## What this controls

Gives every wheel slot a name and a command. Hold the wheel key: a ring expands from the screen center, release runs the slot under the pointer.

## In game

The wheel only opens in online play. The same key does nothing in menus, demo playback or the map editor.

A slot shows the text entered under "Name:"; in game the ring splits into equal angles by entry count — more entries, narrower slots.

Commands run in the console exactly as if typed by hand: `say gg`, `kill`, `connect` all work, chained with `;` for several.

Releasing within 110 distance of the center runs nothing — a dead zone against misfires. Esc closes the wheel without running anything.

## How to tune it

1. Fill in "Name:" and "Command:", then press "Add Bind" — the new entry goes to the end.
2. To edit an entry, select a slot in the preview on the right; both fields load its contents. Edit, then press "Override Selected".
3. An empty name is saved as `*` and shows as a lone asterisk in game; an empty command makes the slot do nothing.
4. Pick a key for "Bind Wheel Key". The default is Q.

With "Reset position of mouse when opening bindwheel" ticked, the pointer returns to center after every use. Off by default, which keeps the last direction and makes repeating the same slot faster.

## Watch out

- 64 entries max. Past a dozen or so each slot holds about twenty degrees; a slight twitch jumps to a neighbor.
- Card changes enter memory immediately and reach the file only when the config is saved. No undo.
- Commands are not validated. A typo surfaces only on release, as a console error.
- Long names cover neighboring slots; the ring does not shrink text to fit.

## Related

- [Bind wheel preview](deck:tclient-bind-wheel-preview)
- [Key binds](qm:key_binds)
