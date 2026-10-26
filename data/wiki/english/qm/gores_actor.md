## What this controls

The moment you freeze or step into a death tile, it sends a preset line to public chat and can fire an emote at the same time. Messages go out as normal public chat, no different from typing by hand, and everyone can see them.

## In game

It triggers on the frame you freeze or fall into the water; sitting still inside does not resend. Main and dummy are timed separately, so both falling in at once may produce one line each. The line is drawn at random from the sentences you wrote, and even a hit still has to pass "Send probability".

## How to tune it

1. Turn on "Auto chat in water" and write a few short lines under "Chat message", separated by commas; leaving it empty means nothing is sent.
2. "Send probability" defaults to 30% — lower it if it spams, raise it if it is too rare.
3. "Send emoticon in water" is on by default with "Emoticon ID" defaulting to 7; turn the toggle off if you do not want the emote.

## Watch out

- Chat has a 5-second cooldown and emotes a 3-second one; triggers inside a cooldown are dropped outright, so several quick falls may produce only one message.
- It works on every server, not just Gores maps; turn off "Auto chat in water" if you do not want it on regular maps.
- The message limit is about 128 bytes; when you cram in too many lines the later ones may fail to send.

## Related

- [Gores mode](qm:gores)
- [Gores drown board](qm:gores_drown_board)
