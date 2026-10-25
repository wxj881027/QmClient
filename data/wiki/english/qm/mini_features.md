## What this controls

A set of unrelated small switches gathered into one card: chat input, scoreboard badge, map coloring, asset fallback and updates. Each takes effect on its own, applies immediately, and never needs a reconnect.

## In game

With "Show command completion in chat" and "Chat emoji completion" on, candidates appear while you type in chat. With "Message merging" on, identical messages sent by the same player within two seconds merge into one line instead of flooding the chat. With "Random emoticon" on, getting hammered or hit by a grenade sends a random emoticon automatically. With "Combo" on, hooking or hammering other players within a short time pops a notification. "Hide input emoticon" keeps the emote bubble off your head while you type. "Show tune zone colors" colors the different tune zones of a map; "Show Qm badge" appends a Qm to the names of QmClient players on the scoreboard.

## How to tune it

| Switch | What it affects | Trade-off |
| --- | --- | --- |
| Command/emoji completion | The candidate bar while typing in chat | Changes your Tab habits |
| Message merging | Identical messages fold into one line | Less flooding; repeated details get merged in |
| Repeat / Random emoticon / Combo | Chat and emote feedback | Can come across as noisy in public rooms |
| Show Qm badge | Spotting QmClient players on the scoreboard | Depends on central-server identification |
| Automatic updates / outdated version warning | The upgrade path | See Watch out below |
| Blank asset auto fallback | Fully transparent sprites fall back to default assets | Turn it off to keep the "hide effects" trick |

## Watch out

- "Automatic updates" only works on Windows: it checks for and downloads new versions in the background, then quits the client and starts the installer once the package is ready. If that interrupts a run or a recording, turn it off and manage versions by hand.
- "Show Qm badge" only marks players identified as QmClient; someone without the badge is usually just on a different client.
- When "Sponsor reminder" is switched off, the dynamic island asks once first; only after you confirm does it stop popping up.

## Related

- [Chat completion](qm:chat_completion)
- [Scoreboard improvements](qm:better_scoreboard)
