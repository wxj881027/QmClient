## What this controls

The "Bind overview" card: a read-only list of every key the client has bound, grouped by purpose — for spotting conflicts, backups or sharing.

## In game

It changes nothing about your controls. Grouping matches the built-in action table exactly: Movement, Weapon, Voting, Chat, Dummy, Miscellaneous — six groups. Anything that fails the match goes to "Other"; the same command with changed case or one extra space drops in there too.

## How to tune it

1. The search box filters both key names and command text, case-insensitive.
2. "Sort by key" / "Sort by command" switches the order of entries inside a group.
3. "Copy all" copies the current filter result, one `key = command` per line.

## Watch out

- What you copy is a reference sheet, not executable bind commands.
- When one key chains several commands, the whole chain must match the built-in action table word for word to be grouped: `+show_chat; chat all` counts as Chat, one more command after it and the entry goes to "Other".
- This card is read-only; make changes in the "Bind" card.

## Related

- [Bind](qm:bind_editor)
- [DDRace HUD Pro](qm:bind_status_hud)
