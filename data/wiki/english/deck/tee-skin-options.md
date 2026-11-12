## What this controls

Where skins are downloaded from and which ones the list allows: download switches, the vanilla-skin filter, fat skins, metadata, and the name prefix.

## In game

The most direct one is the prefix: with `santa` set, any skin named `cammo` is first looked up as `santa_cammo`, and only falls back to `cammo` when that is missing. It applies to all Tees.

Fat skins stretch the body to 1.3x horizontally and vertically; feet and hitbox unchanged.

With "Vanilla skins only" on, non-vanilla skins disappear from the list, and players wearing them appear in default looks.

## How to tune it

| Item | When on | Default | Turn it off when |
| --- | --- | --- | --- |
| Download skins | Missing skins are fetched from the official source and stored in downloadedskins | On | On a limited connection |
| Download community skins | Falls back to the community source when the official one returns 404 | Off | You don't want third-party fetches |
| Vanilla skins only | Non-vanilla skins don't load and don't enter the list | Off | You want to see others' fancy skins |
| Fat skins (DDFat) | Body scaled to 1.3x | Off | The proportions look wrong |
| Show skin date and author | Adds a line with author and release date to list rows | Off | The list is too crowded |

"Skin prefix" is a 12-character input. A mistake doesn't error out — the prefixed version just isn't found and it silently falls back to the original name. The kitty and santa buttons below are one-click presets.

## Watch out

With "Vanilla skins only" on, the prefix is dead: the input stays but nothing you type does anything.

"Download community skins" only matters while "Download skins" is on; both off means skins are never fetched online.

Each download caps at 10 MB; a failed download doesn't overwrite the local cache, and while offline your old skins keep working.

With "Vanilla skins only" on, the skin in use and favorites stay listed, just marked as not found.

## Related

- [Skin browser](deck:tee-skin-list)
- [Skin queue](deck:tee-skin-queue)
