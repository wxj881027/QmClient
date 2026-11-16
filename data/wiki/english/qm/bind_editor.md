## What this controls

The "Keyboard & mouse" card under Settings → Controls → Bind: write one command, or a chain of commands, onto a key. The layout mirrors a real keyboard; mouse buttons and the four wheel directions sit below it, and bound keycaps are highlighted.

## A bind has three parts

`bind <key> "<command>"`

- **Key**: letters and digits as-is; numpad keys take a `kp_` prefix; mouse buttons are `mouse1`, `mouse2`, `mwheelup`.
- **Modifier**: `ctrl`, `alt`, `shift`, `gui`. `gui` is the Windows key, macOS command, or Linux super; combine as `ctrl+s`.
- **Command**: one command, or a chain.

## Chaining commands

Separate them with semicolons and wrap the whole chain in quotes:

```
+fire; +toggle cl_dummy_hammer 1 0
```

**Semicolons do not mean sequential.** Everything runs in the same frame. There is no sleep, delay or wait command. To order actions you must either use separate keys or `exec` one config file after another.

## Three kinds of toggle

| Form | On press | On release |
|---|---|---|
| `toggle setting A B` | switches to the other value | stays; press again to switch back |
| `+toggle setting A B` | writes A | writes B |
| `+toggle_restore setting V` | writes V | restores whatever was there before |

All three work on **client settings only** (`cl_`, `inp_`, `gfx_` and the like); they cannot toggle commands. `+toggle_restore` is an extension in this client — handy for changing sensitivity temporarily without losing your old value.

## Quotes, nesting and length

Binding a `bind` inside a bind needs escaped quotes, doubling the backslashes at every level:

```
bind x "bind y \"+jump; bind z \\\"echo deep\\\"\""
```

Past 5 levels the console errors out. Hand-written nesting is rarely worth it — split it into config files instead.

A whole `bind` line (key name, quotes and escapes included) tops out at **16384 bytes**; the editor refuses to write past that. Typing one by hand in the console is additionally capped by the 512-character input line.

## How to tune it

1. Click a keycap to select it; tick a top-row modifier first, then the main key, for a combo.
2. "Bound functions" lists every command on that key, removable one at a time.
3. "Add function" picks from presets or registered commands, or takes custom input; "Add" writes it.
4. Capture moves the whole bind onto a new key; capturing onto an empty key clears it.

## Verify after writing

Saving validates **syntax only** — quotes closed, brackets balanced. It does not check that the command exists. A typo produces no warning at all; you only find out when the key does nothing.

The reliable check: close settings and press the key, or type `binds <key>` in the console to print whatever that key currently holds.

## Common mistakes

| Symptom | Cause |
|---|---|
| Nothing happens at all | Command name misspelled, or it only works in a specific mode / server |
| State does not revert on release | `toggle` used where `+toggle` was meant |
| Dummy keeps hammering after switching off | The off-side never reset `cl_dummy_hammer` — the exact trap the official deep fly tutorial warns about |
| A key stopped working | Later writes overwrite earlier ones on the same slot; check "Bind overview" |

## Further reading

- DDNet Wiki · Binds: the authoritative description of syntax, nesting and both toggle forms
- DDNet Wiki · Settings and Commands: the dictionary of every command and setting, with defaults

## Related

- [Bind overview](qm:bind_overview)
- [Key Bindings](qm:key_binds)
