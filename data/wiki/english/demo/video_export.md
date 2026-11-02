This page explains the i at the top right of the "Video export" group in the render-replay popup.

Export renders the playing demo into a video file. It does not play in real time — it runs as fast as the encoder can, so a 5-minute demo usually finishes in tens of seconds to a few minutes, depending on encoder, resolution and quality. While exporting, the client can barely do anything else, so run the export on its own.

## Three choices to start

The first export only needs three changes; keep the rest at their defaults:

1. **Export preset**: for everyday sharing pick Daily; for smaller uploads pick Efficient sharing; for handing to editing software pick Editing or Best quality; for a Mac edit suite or spec-compliant delivery pick Exchange; when speed matters most pick Fastest.
2. **Output resolution**: the default Follow window is the current window resolution. To export a smaller size, pick the target value directly — it renders at window resolution and then scales down, giving smoother edges.
3. **Encoder**: the default Default (software) is x264, best quality and slowest. Entries containing NVENC, AMF, Quick Sync or VideoToolbox are GPU hardware encoders — several times faster, quality close at the same bitrate. The list only shows entries this machine can actually use.

## Output resolution

The size of the finished file. Follow window tracks the current render resolution; you can also pick a preset or use Custom to enter width and height.

When you pick a size smaller than the window, the frame is rendered at window resolution first and then scaled down — that is supersampling: exporting 720p from a 1080p window trades more sampling points for smoother diagonals and fewer jaggies. A size larger than the window is pointless; the extra detail is not filled in out of nothing.

## Encoder

How the video is compressed. Software encoding has the best quality and the slowest speed; hardware encoding is several times faster, at the cost of a small gap at the same bitrate, usually invisible.

The gap is most visible at 4K output: measured at 4K60, x264 reaches only about 38 fps, short of 60, while hardware encoding gets past 50 fps.

The list is probed; encoders that cannot open never appear. After a GPU driver change or an external GPU being plugged or unplugged, if an encoder disappears, restart the client and look again.

## Quality

0 to 51; lower is clearer and the file is larger. Leave empty to reuse the video encoding CRF from the settings page.

Encoders name the scale differently but it points the same way: software calls it CRF, NVIDIA CQ, AMD QP, Intel ICQ. Software CRF18 and hardware CQ23 land close in actual quality, so do not copy the number across encoders.

With ProRes this entry is not editable; its quality is fixed by the profile.

## Chroma sampling

The density of color information in the picture. This one affects editing quality more than Quality does.

- 4:2:0: every four pixels share one set of color information. Smallest, most compatible — use this for direct sharing.
- 4:2:2: full color information kept horizontally, the common intermediate format for editing software.
- 4:4:4: every pixel has its own color information. Sharpest for thin lines, small colored text and UI outlines; about 1.3x the size, and some players and hardware decoders do not support it.

The client's render readback is 8-bit, so switching to a 10-bit format adds no detail; the gain comes from color density itself. Measured on the same picture, 4:2:0 and 4:4:4 differ by more than 13 dB of chroma fidelity — visible to the eye on thin lines and colored text.

With 4:4:4, if the encoder does not support it, the export errors out immediately instead of silently downgrading. On that error, switch to a software encoder.

## Container

- MP4: the most compatible; use it for uploads and sharing.
- MOV: the common interchange container for editing software; ProRes requires it.
- MKV: no encoding restrictions in the container itself, but editing software support is poor — not recommended for editing.

## Export preset

Configures encoder, container and quality in one go. The dropdown has seven entries: picking anything other than Custom writes encoder, container, GOP, chroma sampling and quality together, and you can still switch back to Custom to fine-tune each one; changing the container by hand also returns to Custom.

- Custom: keeps all your current settings.
- Daily (H.264 / MP4): software x264 + MP4, ready to upload right after export.
- Efficient sharing (HEVC / MP4): HEVC + MP4, smaller at the same quality. Prefers the first available hardware among NVENC, Quick Sync, AMF and VideoToolbox, falling back to software when none exists.
- Editing (all-intra H.264 / MP4): every frame is a keyframe, so editing software can jump anywhere instantly and cut at any point, while still far smaller than ProRes.
- Exchange (ProRes 422 HQ / MOV): for Mac edit suites or spec-compliant delivery. For an 8-bit source it is not clearer than Editing, just larger and more universal.
- Best quality (all-intra 4:4:4 / MP4): the Editing combo with quality pushed down to 14 — the visually lossless tier, noticeably larger than Editing.
- Fastest (hardware H.264 / MP4): H.264 hardware first, quality 23 traded for encoding speed.

## Keyframe interval

How many frames between keyframes. Edit points can only land on keyframes, so this value directly decides where you can cut.

- 0: the encoder's default.
- 1: all-intra, every frame a keyframe, any cut point lands. Use this when handing to editing software.
- 60: one keyframe per second at 60 fps; enough for a shared finished video.

NVIDIA hardware encoders do not accept 1; they drop to 2 automatically (a keyframe every other frame) and leave a line in the log. For strict all-intra, use a software encoder.

## Export range

Optional, in seconds. Fill in start and end to export only that span; leave empty for the whole demo. Useful for pulling one stretch out of a long demo to check repeatedly.

## Common questions

**Why is the ProRes file so large without looking any better?**

ProRes is an interchange format; its value is that any editing software can open it directly and scrub the timeline without stutter, not higher quality. The client's readback is 8-bit, so ProRes's 10-bit precision holds the same information. If you want more clarity, switching Chroma sampling to 4:4:4 works better.

**Why does scrubbing stutter in my editing software?**

The file uses a normal GOP, for instance the Daily preset. Scrubbing has to decode forward from the previous keyframe. Re-export with the Editing preset.

**Why does a 4:4:4 file fail to open in some players?**

4:4:4 H.264 is High 4:4:4 Predictive profile, which some hardware decoders and mobile players do not support. If it has to play everywhere, use 4:2:0.

**The export is slower than the demo's length — is that normal?**

Yes. Export speed depends on the encoder, not the demo length. 4K plus software encoding is the slowest combination; switching to hardware encoding or dropping to 1080p is much faster.
