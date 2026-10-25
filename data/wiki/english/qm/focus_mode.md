## What this controls

Zen mode is a set of master switches over visuals, sound and chat: once the master switch is on, the sub-items grouped below each take effect — hide HUD, names and effects, mute sounds, filter chat. It changes only what you see locally, not game logic; the you the server sees stays the same.

## In game

- Interface group: hide HUD, map progress, kill/finish messages, scoreboard; hiding the HUD takes the status bar with it.
- Player group: hide names, nameplates, direction indicators, guide lines — the world becomes a crowd of Tees without names.
- Visual group: six effect types, each independent — jump, death/respawn, explosion, freeze, hammer, weapon muzzle; an unticked effect is simply not generated.
- Audio group: mute jump, death/respawn and hammer sounds.
- Chat group: four message types filtered separately — player messages, join/version prompts, server prompt notifications (including the notification bar), Echo messages.
- When the master switch is toggled, one line "Zen mode: on/off" appears in chat.

## How to tune it

| Goal | Tick |
|---|---|
| Clean screen | The four interface items |
| Less distraction | The four player items |
| Quiet | The three audio items |
| Fully silent chat | The four chat items |

All sub-items default to off: with only the master switch on, nothing changes on screen, so tick them group by group. At the bottom of the card, bind a key to "Zen mode key" to toggle it with one press in game; before a key is bound you can only switch it in the settings page or the console.

## Watch out

- Recordings do not follow: while recording, this whole set of rules is disabled and the picture is still drawn per your real settings. To record a clean screen, turn the corresponding settings off directly.
- Effects like hidden HUD or names work by temporarily rewriting your original settings; leaving Zen mode restores the values you saved yourself, and the hidden state is never written to the config.
- Chat filtering is split by type; for a fully silent chat tick all four — one switch is not enough.

## Related

- [HUD notifications](qm:hud_notifications)
- [TClient HUD](tclient:hud)
