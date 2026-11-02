## What this controls

Four independent visual toggles: smaller tees, jelly tees, fake CTF flags, and showing moving tiles in the entities layer. All of them only change what you see — physics and hitboxes are untouched.

## In game

- Smaller tees draws characters smaller: body scaled by 0.7 and feet by 0.85, with the hitbox completely unchanged, so the actual footprint stays the original size.
- Jelly tee deforms characters with their movement: squashed on landing, leaning into stops, whipped around on turns, with a spring-like bounce back.
- Fake CTF flags draws a red or blue flag on every player on the field, purely decorative.
- "Show moving tiles in entities" requires the overlay entities layer fully enabled: it draws the map's moving decorative tiles (flowing water, for instance) into the entities view, so their positions become readable in the grid.

## How to tune it

- "Smaller tees" has three levels: None / Self / All. The "Tiny Tee Size" slider (85–115%) only appears once enabled; it multiplies on top of the already-shrunk size, so even 100% never returns to the original size — only "None" does.
- With "Enable Jelly Tee" on, tune "Jelly strength" (0–1000, default 500) and "Jelly duration" (default 30, roughly 0.2–5 seconds); "Jelly others" makes the whole field wobble too — turning it off removes a lot of noise on crowded maps.
- At max strength the deformation is capped: it only gets shakier, never bigger.
- On race maps, be careful picking "All" for smaller tees: teammates and enemies both shrink at a distance and get harder to tell apart.

## Watch out

- An unchanged hitbox means a shrunk player occupies more space than they look — mind that when squeezing through compressed corridors.

## Related

- [Tee glow](deck:tee-glow)
- [Tee trails](tclient:tee-trails)
