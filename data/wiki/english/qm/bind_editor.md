## What this controls

The "Bind" card in the settings: attach commands to keyboard keys, mouse buttons or the scroll wheel. The layout mirrors a real keyboard, with mouse buttons and the four wheel directions below; bound keycaps carry a thin color bar on top.

## In game

Pressing a key runs the commands bound to it. `+` commands stay active while held and revert on release: `+jump` jumps until you let go, `+fire` fires while held. Ctrl, Alt, Shift and Win combos each get their own slot, independent of bare keys.

## How to tune it

1. Hover a key to select it; tick a top-row modifier first, then the main key, for a combo.
2. "Bound functions" lists each command on the key, removable one by one.
3. "Add function" picks from presets or console-registered commands, or takes custom input; "Add" writes it.
4. The capture row moves the whole bind to a new key; capturing onto an empty key clears it.

## Watch out

- One key can chain several commands, written as one semicolon-joined bind; past the config single-line limit (16384 bytes) you only get an error, nothing is written.
- Saving checks syntax only, not whether the command is right — press the key once on a real server after configuring.
- An existing bind with an unclosed quote is marked unsafe to edit; fix it in the console.
- Binding onto an occupied key replaces its old bind; after big changes, verify in "Bind overview".

## Related

- [Bind overview](qm:bind_overview)
- [Key Bindings](qm:key_binds)
