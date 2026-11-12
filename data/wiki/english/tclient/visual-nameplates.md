## What this controls

A set of extra displays for nameplates and frozen players: ping dots, country flags, skin names, freeze-countdown stars, colored frozen skins, a katana, and white feet. All rendered locally.

## In game

- Ping dot: a small circle next to the nameplate, colored green to red by the other player's ping (capped at 300ms) — who is lagging shows at a glance.
- Country flags: shows the other player's flag on the nameplate; not shown for you or your dummy, nor for players without a country set.
- Skin names: an extra line on the nameplate with the other player's skin name, for hunting missing skins; your own is not shown.
- Freeze stars: every second off the freeze countdown, a burst of stars pops around the frozen player — as many stars as seconds remaining.
- Colored frozen skins: frozen players no longer all turn into the white ninja; they keep their own skin colors.
- Katana: draws a katana in the hands of frozen players, restoring the old behavior.
- White feet: all custom-colored feet are uniformly replaced with one chosen skin's feet, for a tidier look across the field.

## How to tune it

- Seven independent toggles; tick one and that one appears.
- With "Render all custom colored feet as white feet skin" on, a skin name input appears (default x_ninja) deciding which skin supplies the white feet; it only affects skins with custom colors — textured skins are untouched.

## Watch out

- Freeze stars need the server to provide freeze countdown data; on some servers they never appear.
- Nameplate rows, font size and offsets live on the nameplate settings card, not this one.

## Related

- [Nameplate settings](deck:appearance-name-plate-settings)
- [Tee status bar](tclient:tee-status-bar)
