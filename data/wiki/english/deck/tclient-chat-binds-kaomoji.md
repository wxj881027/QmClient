## What this controls

The trigger words of five built-in kaomoji quick phrases. Type the trigger in the chat box and press Enter, and the client sends the kaomoji for you — no typing brackets and underscores by hand.

There are exactly five entries: !shrug, !flip, !unflip, !cute, !lenny. Only the trigger word in front can be changed here.

## In game

Type `!shrug 在忙` and press Enter, and what goes out is 「在忙 ¯\_(ツ)_/¯」 — your words first, the kaomoji after. Typing just `!shrug` sends only the kaomoji.

The trigger must be the first word of the line and match what is set here exactly (case-insensitive). `hi !shrug` does not trigger; it goes out as ordinary chat, verbatim.

A line that triggers is not sent as chat, but it does enter the chat history — press arrow-up to recall it.

## How to tune it

1. Shorten the triggers, say !s, !f, to save a few keystrokes. Once changed, the old trigger stops working at once.
2. Keep the five from colliding. If two match, only one of them ever fires; the other never gets a turn.
3. After changing, send one from the chat box — the syntax is obvious at a glance.

## Watch out

- Only Enter in the chat box triggers them; typing the same text in the console does nothing.
- Emptying a trigger deletes that quick phrase. A `unbindchat` line is appended to the save file, and it will not come back on its own next launch.
- Triggers cannot contain spaces; matching only reads the first word.
- The kaomoji themselves are hard-coded into the commands and cannot be changed here.
- Chat input is capped at 256 bytes; beyond that nothing more can be typed, and it is never split into two messages.

## Related

- [Other chat binds](deck:tclient-chat-binds-other)
- [Chat controls](deck:controls-chat)
