## What this controls

The trigger words of four built-in quick phrases: !translate, !translateid, !mute, !unmute. They turn what you type in the chat box into a console command, not a sent message.

## In game

`!translate Bob` translates Bob's last message; no name means the latest one.

`!translateid 12` takes a player number directly, no name typing.

`!mute someone` adds a player to the war list and their chat stops showing for you; `!unmute someone` removes them.

## How to tune it

Shorten the triggers, say !t, !m; the old one expires at once. Write names in full; the clan is an optional second argument.

## Watch out

- Arguments pass to the command verbatim, extra spaces included.
- Only Enter in the chat box runs them; the console does not count.
- On failure only the console speaks; the chat box shows nothing.
- A misspelled name is still added; it just never matches, no warning.
- If two triggers collide, one of them stops working.

## Related

- [Kaomoji quick phrases](deck:tclient-chat-binds-kaomoji)
- [War list quick phrases](deck:tclient-chat-binds-warlist)
