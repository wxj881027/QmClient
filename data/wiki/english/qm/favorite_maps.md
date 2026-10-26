## What this controls

A favorites list that stores map names only. This card lists, copies and removes entries; adding happens elsewhere. The favorites tab in the server browser and the vote menu read the same data.

## In game

Tick "Favorite map" on the in-game server info page and the map appears in this card with its type (Novice, Brutal, Classic and so on). Click a map name to copy it to the clipboard, handy for sharing or searching; click the X on the right to remove it. Types come from the community categories of the server list; a map never seen there shows as "Unknown".

## How to tune it

| Goal | How |
| --- | --- |
| Add a favorite | Join a server and tick "Favorite map" on the server info page, or mark the map as a favorite in the vote menu |
| Copy a map name | Click the name in this card; "Copied" shows for 1.5 seconds |
| Remove a favorite | Click the X on the right of the entry; immediate, no confirmation |

You can also add and remove directly from the console with `add_favorite_map` and `remove_favorite_map`.

## Watch out

- Favorites match map names exactly: once a name changes, the old entry no longer matches that map.
- The card lists at most 64 entries; favorites beyond that still work, they are just not shown here.
- Removing only deletes the name from the list, not the map file; the favorites tab loses the entry in sync.
- Type detection depends on a scan of the online server list; maps you joined while offline may stay "Unknown".

## Related

- [Map upload](qm:map_upload)
- [Console](qm:console)
