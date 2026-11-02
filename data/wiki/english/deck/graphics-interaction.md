## What this controls

The master switches for whether the interface animates and for how long, plus card entry, card height changes, card reflow, in-game presentation animations and the scroll wheel feel. It changes transitions only — not layout results, and not game logic.

## In game

- With "UI motion level" set to Off, menu and settings page switches, expands and collapses all finish instantly, with no fades or sliding.
- "Presentation animations" governs the in-game chat box, emote selector, scoreboard and spectate switching; on, these open and close with a transition; off, they pop straight in.
- With "Original scroll feel" on, every wheel tick starts scrolling from rest and inherits no speed from the previous tick — choppier, but not floaty.
- Raising "Scroll step" makes one wheel tick travel farther; holding Alt scrolls 3x.

## How to tune it

| Level | Behavior |
| --- | --- |
| Off | Everything finishes instantly; the toggles below stay ticked but do nothing |
| Reduced | Shorter, harder transitions; card entry moves less |
| Full | Full transitions play according to each toggle below (default) |

On the settings card pages, one wheel tick travels 12x "Scroll step". If the settings page scrolls too fast, shrinking the step helps more than turning animations off.

## Watch out

- Off does not clear the toggles below; switching back to Full restores them as they were.
- "Presentation animations" is off by default, and it is independent of "UI motion level" — picking Full does not enable it automatically.
- Reduced stops the decorative animations on cards and keeps only the most essential transitions.
- "Scroll step" accepts 2–100, default 10; once set very high, long lists tend to overshoot in one tick.

## Related

- [Visual effects](deck:graphics-visual)
- [Chat](deck:appearance-chat-settings)
