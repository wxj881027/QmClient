# Qm Icon Atlases

This directory holds the generated Qm UI icon atlases:

- `qm_icons_{light,regular,bold,fill,duotone}_msdf.png` with matching JSON manifests
- Thin is not bundled (no Phosphor-Thin font); icon weight 2 reuses the light atlas.

The atlases are baked from the bundled Phosphor TTF fonts (full icon set) plus the
official name→codepoint mapping in `datasrc/qm_icons/phosphor.codepoints` via
`qmclient_scripts/qm_build_icon_msdf_font.py` (see the `qm-icon-msdf-atlas` CMake target).
The duotone atlas keeps the `secondary_mask: "alpha"` contract: RGB = primary MSDF,
Alpha = secondary true SDF.

The JSON manifest stores icon IDs (official Phosphor names) and pixel rects.
`CQmIconManager` converts those rects to UV coordinates at runtime and renders via
the textured-MSDF pipeline. When the atlas is unavailable (backend without MSDF or
missing assets), icons fall back to the Phosphor TTF glyph path (`FontIcons`); there
is no bitmap fallback atlas anymore.
