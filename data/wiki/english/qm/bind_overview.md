## What this controls

The "Bind overview" card: a read-only table of every current bind, grouped by purpose. Use it to check what a key actually holds, whether something overwrote it, and to export the whole set for reference.

## Reading a row

`key = command`. When one key chains several commands you see the full chain, not one row per command.

## Where the groups come from

An exact, word-for-word match against the built-in action table: Movement, Weapon, Voting, Chat, Dummy, Miscellaneous. Anything that fails goes to "Other".

The match is strict, so `+show_chat; chat all` counts as Chat while adding one more command drops it into "Other"; different case or an extra space drops it too. That is not a classification bug — it is telling you the bind is no longer a standard form.

## Three controls

1. **Search** filters key names and command text together, case-insensitive. Fastest way to find which key holds an action.
2. **Sort** switches between "by key" and "by command" inside each group.
3. **Copy all** copies the current filter result as one `key = command` per line.

## Two problems it solves

- **A key was overwritten**: the key no longer does what you remember. Search here to see what it holds now and which card or preset took it.
- **Duplicate bindings**: the same action on two keys, which tends to fight with itself when switching weapons or dummies.

## What the copied text is for

It is a **reference sheet, not a config**. Pasting it into chat, a document or a bug report works fine; turning it into executable binds means adding the `bind ` prefix and quotes yourself, or writing them in the "Bind" card.

## Read-only on purpose

Nothing here writes. All edits happen in the "Bind" card, so there is exactly one answer to "where do I change this".

## Related

- [Bind](qm:bind_editor)
- [Key Bindings](qm:key_binds)
- [DDRace HUD Pro](qm:bind_status_hud)
