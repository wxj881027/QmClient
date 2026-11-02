## What this controls

Twelve chat quick phrases for adding players to or removing them from the war list. Type a trigger, a space and a name in the chat box and press Enter — the line is not sent; it becomes a command.

## In game

After `!war someone` and Enter, that player's name switches to the war-group color, on the scoreboard and the nameplate alike, without opening any settings. Made a mistake: `!delwar` undoes it and the color reverts.

## How to tune it

| What you want | What to type |
|---|---|
| Add a war player | `!war nick` |
| Add a war clan | `!warclan nick of an online player` |
| Add a friendly player | `!team nick` |
| Add a friendly clan | `!teamclan nick` |
| Undo any of the four above | `!delwar`, `!delwarclan`, `!delteam`, `!delteamclan` |
| Set the group explicitly | `!name group player reason` |

The reason may be omitted. The twelve input rows on the card change each phrase's trigger word; emptying one deletes that phrase, and the command behind it stays.

## Watch out

- Triggers are case-insensitive but must match whole words — `!wars` does not hit `!war`.
- The clan versions do not take a clan name; they look up which clan the named online player currently wears. If the player is offline nothing happens and no error is shown; if they are online without a clan you get one line of feedback.
- Names cannot contain spaces; every word after the space counts as the reason.
- Duplicates are allowed by default: add the same player twice and the list holds two entries.

## Related

- [War list](deck:tclient-warlist)
- [Other chat binds](deck:tclient-chat-binds-other)
