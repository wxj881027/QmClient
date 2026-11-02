## What this controls

Display-layer sanitizing for streaming: replaces non-friend names, clans, skins, flags and sensitive words in chat on your screen, without changing anything on the server.

## In game

The rule is uniform: "not a friend and not yourself". On a hit, the name becomes the client ID, the clan is cleared, the skin is swapped for the default skin and scoreboard flags use the default style. Sensitive-word hits are overwritten in place with `***`, leaving the surrounding text alone, across names, chat, kill messages, console echo and the notification area. Skin changes also skip the transition animation in streamer mode, so the original skin never flashes for a frame. Friends and yourself are untouched — familiar names on screen are expected.

## How to tune it

| Toggle | Changes | Typical use |
| --- | --- | --- |
| Filter sensitive words | Sensitive word fragments in chat | Built-in list, automatic |
| Replace non-friend names with ID | Names, clans | Keep other players' nicknames off screen |
| Replace non-friend skins with default | Skins | Skins are other people's personal content |
| Use default flags on scoreboard | Flags | Flags can reveal a country |

To add words, fill in "Word list file": UTF-8, one word per line, `#` starts a comment, up to 2 MiB. Click "Reload" after editing; the file list merges with the built-in list.

## Watch out

- It only affects non-friends, and the check follows your friend list — add someone as a friend and their name and skin show up immediately.
- A word-list hit only replaces the text with `***`; chat still goes to the server as usual and everyone else sees the original.
- "Filter sensitive words" affects both chat and the notification area; with it off, both show the original text.

## Related

- [Word filter](qm:block_words)
- [War list](deck:tclient-warlist)
