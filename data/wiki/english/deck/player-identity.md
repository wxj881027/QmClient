## What this controls

Name and clan tag for the main tee and the dummy. They decide what others see on the scoreboard, in chat and on name plates; skin, flag and team untouched.

## In game

Both inputs truncate by byte: name 16 bytes (15 letters or 5 Chinese characters), clan 12 bytes (11 letters or 3 Chinese characters); extra input is blocked.

Changes are sent once, when you close the settings menu, not while typing. The server trims leading and trailing whitespace, renames duplicates to (1)name, kicks on a name-ban hit, and announces renames in public chat.

## How to tune it

1. Keep the name short, leaving room for other players' prefixes and team marks.
2. Abbreviate the clan; empty means hidden.
3. Main tee and dummy are set separately under the "Player" and "Dummy" tabs at the top; the two sets are independent.
4. Exit the settings page, then check the scoreboard and chat.

## Watch out

Empty is not no name: the main tee falls back "Steam name → nameless tee"; an empty dummy name becomes "[D] main name", and brainless tee when the main has no name either. The gray text in the name field is the fallback name to be used.

By default the server allows one info change every 5 seconds; submissions inside the cooldown are dropped, which looks like the edit not taking effect. Skin and colors share the cooldown.

## Related

- [Choose country flag](deck:player-country)
- [Profiles](deck:tclient-profiles)
