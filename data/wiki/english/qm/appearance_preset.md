## What this controls

"Qm recommended appearance" is a fixed preset that writes 12 interface settings in one go: Latin font and CJK font both set to LXGW WenKai, both weights 400, overall UI scale 100%, UI animations "Full", menu panel / menu accent panel / settings page opacity 30%, background blur on, UI color "dark translucent", menu panel color black. It only touches how the interface looks, not gameplay, account or skins.

## In game

After applying, the menu and settings page immediately turn into translucent dark panels with background blur, and the fonts switch to LXGW WenKai — no restart needed. Setting the overall UI scale back to 100% also scales the in-game HUD along with it.

## How to tune it

1. Before clicking "Apply Qm recommended appearance", check the 12 items listed on the card and the font status: when it shows "LXGW WenKai is available.", fonts and weights are replaced together; when it shows unavailable, the four font items stay as they are and everything else applies as usual.
2. If the panels feel too transparent afterwards, raise the three opacity values individually instead of rerunning the whole table.
3. On a low-end machine, if menu animations cost frames, drop UI animations one step below "Full".

## Watch out

- This overwrites the whole group at once; panel opacity and UI colors you tuned before are replaced directly, with no undo.
- When LXGW WenKai is missing, the preset still applies — only the fonts stay unchanged; that does not mean the preset failed.
- If you had manually scaled the interface up, you need to re-adapt after the overall UI scale returns to 100%.

## Related

- [Tee appearance](qm:skin_appearance)
- [Skin switch animation](qm:skin_transition)
