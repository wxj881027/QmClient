## What this controls

Turns lasers from a two-layer hard-edged beam into a multi-layer beam with glow, with an optional pulse animation. It only changes the picture; hit detection and bounce timing are untouched.

## In game

With the enhancement off, the beam has only two layers: outline plus core, with a hard-cut edge. With it on, the same beam stacks into 13 layers, wide to narrow, faint to solid: the outermost spreads the widest, it gets brighter toward the center, and the innermost layer is pure white.

The hit spot also grows from two layers to five, looking bigger and brighter.

With the pulse on, the whole beam breathes — thick and thin, bright and dim, in turns.

## How to tune it

After turning on "Laser effect enhancement", check "Glow intensity" first, 30% by default. Raising it makes the beam thicker and brighter, at the cost of covering terrain more easily.

"Laser size" defaults to 100% and adjusts the beam's thickness between 50% and 200%. "Opacity" only affects the enhanced beam; at 0 it is invisible.

"Pulse speed" at 100% is about one cycle per second, and "Pulse amplitude" defaults to 50%: at 100% the swell is most visible, at 0 the beam stops breathing.

"Rounded caps" works even without the enhancement, making the beam's ends round instead of flat.

## Watch out

With "Glow intensity" at zero, the whole enhancement path is off, and laser size, opacity and pulse stop working with it — easy to mistake for settings that didn't save.

Pulse speed and amplitude only appear while the enhancement is on.

Without the enhancement, the beam has a fixed width and is fully opaque; changing laser size or opacity does nothing.

More layers cost more GPU in brawls and laser walls; if frames drop, lower glow intensity and laser size first.

## Related

- [Laser colors](deck:appearance-laser-colors)
- [Laser preview](deck:appearance-laser-preview)
