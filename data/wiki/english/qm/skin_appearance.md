## What this controls

How the Tee is drawn on screen: skin outlines, a custom-color hue cycle, emote shadows and the emote projectile duration. Only what your own screen shows; other players cannot see these settings.

## In game

The outline traces the Tee silhouette so you can spot yourself in a crowd; "Skin outline for self and dummy" and "Skin outline for other players" are separate switches. The hue cycle makes a custom-colored Tee drift slowly around the color wheel at an adjustable speed; when the skin itself has no custom color, ticking it changes nothing, and it has no effect while TClient rainbow Tee is on. The emote shadow puts a shadow layer under the emote bubble; "Emoticon projectile duration" decides how long a locally fired emote keeps flying through the air.

## How to tune it

| Width / opacity | Look | Fits |
| --- | --- | --- |
| 1–2 / low | Traces only the silhouette, stays out of the picture | Used to the vanilla look |
| 2–4 / medium | Recognizable even in a crowd | Most of the time |
| 5–6 / high | Figure is very prominent, leaning cartoonish | Wide shots, streaming |

- For the hue cycle, tick "Cycle custom Tee hue" first, add "Also apply to dummy" if the dummy should follow, and set the pace with "Hue speed".

## Watch out

- Outline width caps at 6; opacity 0 is the same as off. At full width the edges blur into a ring.
- Outlines and emote shadows only affect the visuals, never collision.
- The cycle only applies to Tees that actually use custom colors.

## Related

- [Skin transition animation](qm:skin_transition)
- [Rainbow Tee](tclient:rainbow)
