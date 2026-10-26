## What this controls

A purely local roster. Register player names or clan names into groups; each group carries a color, matched players appear in that color on your screen, and you can also block them outright.

## In game

Four things you can see directly:

- Nameplates, scoreboard and chat names switch to the group color. Names in team messages get recolored too.
- The other player's Tee in the world gets a glow ring in the group color, overriding the team glow.
- A line with your "Reason" text appears under the nameplate.
- With "Fully block enemy players" ticked, their name becomes a client number, skin resets to default, chat is hidden. You and your dummy are unaffected.

## How to tune it

| Goal | How |
| --- | --- |
| One player | Click their name in the online players list, then "Add Entry" in the entry editor |
| One whole clan | Click the clan name to their right, or fill it into the "Clan" box yourself |
| Severity levels | Add a group under war groups, pick a color, then add entries to that group |
| Color only, no blocking | Untick "Block enemy chat" |

Name and clan are mutually exclusive; the other box becomes read-only. To edit an existing entry, select it in the list above, then press "Override Entry". Entries added without picking a group land in the default none group, colored white.

The search above the entries list filters by name, clan and group name; the arrow on the right flips the order — newest on top by default.

## Watch out

- Matching is exact; a different case is not a hit. A renamed player breaks the name entry, clan entries survive.
- "Allow Duplicate Entries" is on by default. With several entries for one player, the last one decides the color.
- "Block enemy chat" only applies to the enemy group; people in your own groups still talk.
- Deleting a group asks for confirmation. Its entries are not deleted — they fall back to the none group and turn white.
- The trash can right of an entry deletes immediately, without confirmation or undo.
- Toggling off "Enable warlist" disables all marks at once; the list itself remains.
- "Show colors in spectator selection" is currently read nowhere; ticking it changes nothing.

## Related

- [Warlist chat binds](deck:tclient-chat-binds-warlist)
- [Tee glow](deck:tee-glow)
