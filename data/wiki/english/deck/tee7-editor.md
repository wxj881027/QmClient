## What this controls

The editor for Tee 7 (0.7 protocol) skins: preview on top, "Basic" and "Custom" pickers below, one set each for player and dummy.

## In game

This skin only reaches 0.7 servers; on 0.6 servers you still wear the set from "Player preview". The two sets are stored separately and never displace each other across servers.

The preview draws three versions at once: your own look, red team color, blue team color. The latter two re-render your chosen parts in team colors, to check whether the pattern stays readable once you join a team.

## How to tune it

1. Pick a close full skin under "Basic" first — easier than assembling from scratch.
2. Switch to "Custom" for details and pick a part tab: Body, Marking, Decoration, Hands, Feet, Eyes. Each part lists its own assets; the Eyes thumbnails cycle through emotes on their own.
3. Every part has its own "Custom colors"; the sliders affect that part only.
4. When satisfied, hit "Save" and give it a name to write into the skins7 directory; only then can "Basic" re-select it by name.

## Watch out

After picking individual parts, the full-skin name is cleared and the combination becomes unnamed loose parts. Unsaved, it lives only in the config; picking any full skin next wipes it entirely, with no way back.

Saving writes local files only and uploads nothing. "Skins directory" opens that folder; press F5 or Ctrl+R after changing files to rescan before they show up.

Changes are not sent immediately — leaving the page sends one look update. The server accepts one per 5 seconds by default; if you changed something and want another change, wait a few seconds before leaving.

"Delete" only appears for self-made skins. Once deleted it is gone; there is no recycle bin.

## Related

- [Player preview](deck:tee-identity)
- [Skin browser](deck:tee-skin-list)
