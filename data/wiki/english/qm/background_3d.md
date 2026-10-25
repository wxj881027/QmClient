## What this controls

Draws a set of slowly drifting 3D wireframe shapes in the game background: cubes, hearts, spheres, pyramids, diamonds, rings, stars, crescents, or a mix of several. Purely decorative; it takes no part in any gameplay check.

## In game

- Shapes stay in world coordinates: when the camera pans they sit as if pinned to the map; only a teleport or a large zoom change moves the whole batch.
- They have depth: distant shapes are smaller, fainter and drift slower.
- With "Particle collision" on, a Tee moving close pushes shapes away; they drift back once you leave.
- Particles have a lifetime and fade in and out in a loop; the color can be fixed or randomized each time.

## How to tune it

1. First tick "Enable 3D background particles".
2. For "Particle type", "Mixed" holds up best; a single fixed shape also works.
3. "Particle count" defaults to 45; 15–25 keeps the atmosphere and is the setting that affects framerate the most.
4. "Particle alpha" defaults to 30%; paired with small sizes it blends into the background most easily.
5. "Particle glow", "Particle trail", "Particle pulse" and "Particle twinkle" are all extra segments — on for screenshots, off for matches.

## Watch out

- It draws below the map background layer: on maps with a full background it can be nearly invisible, so don't rush to suspect the toggle.
- If frames drop, lower the count first, then consider turning the trail off; 200 particles plus full trail is the most expensive combination.
- Shapes passing through your Tee are normal: it only reads the camera and never touches collision, hook length or input.

## Related

- [Visual effects](tclient:visual-effects)
- [Skin switch animation](qm:skin_transition)
