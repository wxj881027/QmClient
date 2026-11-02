## What this controls

The editor panel for both your player and dummy looks. Two previews side by side on top; click one to edit that side: skin name, body and feet colors, and the default emote worn on join.

## In game

This changes how you appear in the room. Your own screen updates at once; others see it only after you close the settings menu and that one player info update goes out.

The default emote is not a local effect. Set to anything other than "Normal", joining a server sends an emote alongside /timeout; everyone in the room sees it, and it stays until another emote replaces it. "Normal" sends nothing.

## How to tune it

1. Click the preview you want to edit first; an underline appears and the title reads "Player: name" or "Dummy: name".
2. Type the name under "Your skin"; leaving it empty draws default. Loading shows a spinner; not found shows a red triangle with the reason.
3. For your own palette open "Custom colors" — one row each for body and feet: enter #RGB or #RRGGBB codes, drag the HSL sliders below, or roll the dice button for random colors.
4. The emote slider has six steps: Normal, Pain, Happy, Surprise, Angry, Blink. Hovering a preview makes the Tee happy — that is preview only.

| Button | What it does |
| --- | --- |
| Copy skin from player | Copies the player's whole look to the dummy |
| Swap | Exchanges both looks |
| Copy skin from dummy | Copies the dummy's whole look to the player |

## Watch out

Nothing is sent while you stay on the page. The server allows one info change every 5 seconds by default; a change hitting that cooldown is dropped. Wait a few seconds after editing, then close.

With "Custom colors" off, the color rows vanish and the skin's own palette renders. Your values are kept, just unused.

A misspelled skin name does not fall back to default; the wrong name is what gets stored, and the error mark stays on the preview.

## Related

- [Skin options](deck:tee-skin-options)
- [Team tee glow](deck:tee-glow)
