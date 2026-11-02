## What this controls

The chat translation layer: incoming chat goes to a translation service and the result is attached to that message; your own messages can be translated before sending, so only the translation goes out. It only changes the text you see and what you actually send.

## In game

Incoming messages are language-checked locally first: too few characters, or the target language already dominant enough, and they are not sent for translation — the two thresholds ("Minimum match chars", default 2; "Target language ratio", default 75) decide what counts. Matches enter an async queue handled by the HTTP backend, and the translation is written back onto that chat line; if the line has already been pushed out of the chat ring buffer, it is abandoned. When a translation fails, the line shows a localized error message.

Outgoing messages are translated before they are actually sent, and only the translation reaches the server. With "Show original text below outgoing translation" on (default on), the original stays on your machine as an attached note. If translation fails, nothing is sent — you see an error in chat, and the original is not sent as a fallback.

## How to tune it

1. Turn on "Auto translate received messages" and pick your usual reading language under "Translate received messages to" (default zh).
2. Pick a "Translation service": MyMemory (default, anonymous daily quota without registration), Tencent Cloud, DeepL, Baidu, LibreTranslate, FTAPI, Automatic (Qm service), or LLM API. Each service has its own credentials, and switching means refilling: Tencent Cloud needs SecretId/SecretKey, Baidu needs APP ID and key, DeepL needs an API key, and LLM keys differ per provider. LibreTranslate and self-hosted services can run without a key.
3. Use "Test translation" to verify the setup; it sends one fixed short English sentence. With "Translation diagnostics" on, the service name and HTTP status are shown too.
4. To translate outgoing messages as well, enable "Auto translate sent messages" and set "Translate outgoing messages to" separately (default en).
5. "Advanced options" holds LLM concurrency (0 = auto; Zhipu free tier allows only 1), receive/send translation methods (translate only when needed vs. always), and more.

## Watch out

- Hitting the MyMemory daily quota warns only once, then stays quiet for a minute; sentences over 500 bytes are not sent for translation.
- At the concurrency cap, later messages are skipped outright — they are not queued for later translation.
- FTAPI is a free public service; its auto-translate is off by default and must be enabled manually in the card. Abuse can take the service down.
- After switching LLM models or endpoints, verify with "Test translation" again.

## Related

- [Translate button](qm:translate_ui)
- [Language](deck:general-language)
