## What this controls

Chat auto replies: when a public chat message matches keywords you set, it answers with a preset line. It only sends normal chat messages — it does not parse or execute commands and does not change server behavior.

## In game

When someone mentions a keyword in public chat, the reply appears in public chat. When several rules match at once, one is picked at random; if a single reply lists several candidates separated by |, another random roll happens, so the same keyword can get different answers.

## How to tune it

The panel has three tabs: "Rule list" to add or remove rules and fill in trigger words; "Reply content" to fill in the reply for each rule; "Trigger limits" holds the master switch, "Reply with dummy" and "Auto reply cooldown".

- Separate trigger words with commas, semicolons or vertical bars, full-width or half-width punctuation both fine; with several words written, matching any one counts.
- Tick "Rename" on a rule and the character "Qia" (恰) is appended after your (or your dummy's) name after a successful reply; if the name already ends with it, nothing is added.
- With "Reply with dummy" on and the dummy connected, replies are sent from the dummy.
- "Auto reply cooldown" runs 0–30 seconds, default 3 seconds, shared globally: after one reply goes out, every other keyword also has to wait for the cooldown to end.

## Watch out

- Only public chat is matched; team messages, whispers and system messages never trigger, and your own messages plus anything starting with / are skipped.
- A single reply is capped at about 256 bytes and long text gets truncated; all rules together are capped at about 4096 bytes.
- Very common keywords turn into a fixed reflex; the cooldown only throttles, it cannot stop a reply every single time.
- Trigger and reply must both be filled; a rule with either side missing does nothing, and the panel shows a red hint.

## Related

- [Auto reply](tclient:auto-reply)
- [Block words](qm:block_words)
