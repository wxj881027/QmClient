## What this controls

A local word list: chat lines from players that match a word are masked by replacement or hidden entirely before you see them. This only changes your screen — the original text still goes to the server, and everyone else still sees the original words.

## In game

In replace mode, matched words are swapped for the "Replacement chars" (default `*`). In hide mode the whole line disappears; if a team chat call mentions the word in passing, the tactical information goes with it. Both actions log matched words to the console so you can check for false positives.

Server messages, client notices, your own messages and your own whispers are never hidden and show as-is.

## How to tune it

1. Turn on "Enable word filter list" and type words into the "Word Filter" field. Separate with commas, semicolons, vertical bars or line breaks (full-width Chinese commas and semicolons also count; spaces do not).
2. Under "Behavior" pick "Replace mode" or "Hide player messages". In replace mode you can change "Replacement chars", and "Use multi-char replacement based on word length" makes longer words take more `*`.
3. "Mode" has three options: Regular expression, Literal, Both (default Both). Broken regexes are skipped and logged once.
4. For many words use "Word list file": UTF-8, one word per line, `#` starts a comment, up to 2 MiB and 100,000 entries. Click "Reload" after editing; the loaded word count shows next to the button.

## Watch out

- "Add to blocked words" in the chat line menu adds the full original text of that message, so it only suits blocking whole-line spam.
- Literal matching is a case-insensitive substring match; the longer the list, the more false positives — prune it regularly.
- "Hide player messages" only catches real players: server messages are never replaced or hidden, even when they contain a matched word.
- The word list cache is rebuilt only when the list, mode or file changes; it is not re-parsed otherwise.

## Related

- [Streamer mode](qm:streamer)
- [Chat bubble](qm:chat_bubble)
