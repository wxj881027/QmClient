## What this controls

Voice chat, but the audio travels over a dedicated voice service, not bound to the game connection.

## In game

Three things you can feel directly:

Voice and the game run on two separate paths. When the game lags, drops packets or disconnects, voice may still be up; the other way round, voice dying does not stop you from running the map.

Volume falls off with the distance between the two Tees. Beyond "Voice distance radius (tiles)" the other side cannot hear you at all — not gradually quieter. The default is 50 tiles; standing at the edge is almost silent.

On the sending side there is exactly one gain stage: your own "Microphone volume".

## How to tune it

1. First enable local test mode and speak into the microphone. If you can hear yourself, device and volume are fine; turn it off after the test. This step uses no network and does not pass through the voice service.
2. Turn on "Enable voice", expand "Advanced options", and set "Input device" and "Output device" correctly. An empty device means the system default, which is often wrong after plugging or unplugging a headset.
3. If you don't want a key, enable "Auto unmute when speaking" and the mic opens while you speak; to control the mic yourself, bind a hold-to-talk key to +qm_voice_ptt on the key page.
4. If teammates say you are too quiet, raise "Microphone volume". If you can't hear others clearly, raise "Playback volume" first, and if that is not enough, widen "Voice distance radius (tiles)".

When you hear nothing, check in this order:

| Symptom | Check first |
|---|---|
| No sound at all | Whether "Enable voice" is on, whether "Output device" is set wrong |
| Only one person is silent | Whether your distance exceeds the radius, whether that person is muted |
| Teammates can't hear you | "Input device", whether the talk key is held, "Microphone volume" |
| Intermittent audio | The connection status on the card and the network, not game latency |

## Watch out

Noise reduction, AGC, compressor and limiter under Advanced are currently not in the send chain; adjusting them makes no difference. The only thing that takes effect is "Microphone volume".

Beyond the radius, whole packets are discarded. So widening the radius helps more than raising the volume; volume only affects loudness inside the radius.

Stereo is off by default. When enabled, the left and right channels split depending on whether the other person is on your left or right; on mono headphones you will not hear any difference.

The voice server address can be changed, but changing the service means changing the room; both sides must change it to hear each other.

## Related

- [Key binds](qm:key_binds)
- [Chat and team chat](deck:controls-chat)
