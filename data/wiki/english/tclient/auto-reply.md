## What this controls

Chat auto-reply: when someone whispers you or points at your name in public chat, it answers for you with a fixed line. One reply goes to players you have blocked, one fires when you are away from the window; both are sent as normal chat.

## In game

- The trigger is a message aimed at you: a private message, or your (or your dummy's) name appearing in public chat. Ordinary public chat and system messages get no reply.
- If the sender is on your block list (enemy list or chat mute) and still messages you, the line from "Automatically reply to muted players" is sent.
- If you are named while the window has lost focus, the line from "Automatically reply while the window is unfocused" is sent. Switching to another program counts as unfocused.
- When the server has a /w command or the message came in as a whisper, the reply goes out as a private message; otherwise it degrades to a public reply in "name: text" form.

## How to tune it

1. Turn on "Automatically reply to muted players" and fill in a short line, for example "I muted you".
2. Turn on "Automatically reply while the window is unfocused" and say roughly when you will be back.
3. Each text is capped at about 40 Chinese characters; longer text cannot be saved, keep it short.

## Watch out

- The reply goes out verbatim, including to players you have blocked. Think about how it reads before you write it.
- A reply identical to your previous message is not sent again, so the server does not treat it as spam.
- The block check runs before the unfocus check; once the first one hits, the second is skipped.

## Related

- [Block words](qm:block_words)
- [Other chat binds](deck:tclient-chat-binds-other)
