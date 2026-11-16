## What this controls

The master switches for whether the interface animates and for how long, plus card entry, card height changes, card reflow, in-game presentation animations and the scroll wheel feel. It changes transitions only — not layout results, and not game logic.

## In game

- With "UI motion level" set to Off, menu and settings page switches, expands and collapses all finish instantly, with no fades or sliding.
- "Presentation animations" governs the in-game chat box, emote selector, scoreboard and spectate switching; on, these open and close with a transition; off, they pop straight in.
- "Scroll feel" chooses between Original (restart every tick), Smooth (velocity carries over) and Spring (damped spring). Picking Spring reveals three tuning rows, detailed below.
- Raising "Scroll step" makes one wheel tick travel farther; holding Alt scrolls 3x.

## How to tune it

| Level | Behavior |
| --- | --- |
| Off | Everything finishes instantly; the toggles below stay ticked but do nothing |
| Reduced | Shorter, harder transitions; card entry moves less |
| Full | Full transitions play according to each toggle below (default) |

On the settings card pages, one wheel tick travels 12x "Scroll step". If the settings page scrolls too fast, shrinking the step helps more than turning animations off.

## Scroll feel in detail

"Scroll feel" only changes how the offset reaches its target; the distance per tick still comes from "Scroll step".

| Mode | Behaviour |
| --- | --- |
| Original | Every tick starts from rest and inherits no speed — closest to the original client, choppier but never floaty |
| Smooth | Consecutive ticks add their velocity into one continuous glide, so fast wheeling carries farther |
| Spring | A damped spring drives the offset: reversing direction slows down before it turns, which reads as weight |

The three rows below only appear while Spring is selected:

| Row | Range | What it does |
| --- | --- | --- |
| Spring response | 80–600 ms | How long the spring takes to settle on a new target; lower reaches it sooner, higher feels heavier |
| Spring damping | 50–100 % | How firmly it settles; 100 stops without overshoot, lower values let it bounce past the target |
| End bounce | 0–64 UI px | How far a wheel tick or a drag can push past an end; the content springs back once scrolling stops; 0 disables it |

"End bounce" is measured in UI pixels, so it follows the interface scale rather than the window resolution.

## Watch out

- Off does not clear the toggles below; switching back to Full restores them as they were.
- "Presentation animations" is off by default, and it is independent of "UI motion level" — picking Full does not enable it automatically.
- Reduced stops the decorative animations on cards and keeps only the most essential transitions.
- "Scroll step" accepts 2–100, default 10; once set very high, long lists tend to overshoot in one tick.

## Related

- [Visual effects](deck:graphics-visual)
- [Chat](deck:appearance-chat-settings)
