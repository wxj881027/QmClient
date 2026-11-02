R"JS((() => {
const bridgeVersion = "14";
if (window.__QM_NCM_BRIDGE_VERSION__ !== bridgeVersion) {
  if (window.__QM_NCM_BRIDGE_TIMER__) {
    try { clearInterval(window.__QM_NCM_BRIDGE_TIMER__); } catch (_) {}
    window.__QM_NCM_BRIDGE_TIMER__ = null;
  }
  const previousSubscription = window.__QM_NCM_PROGRESS_SUBSCRIPTION__;
  if (previousSubscription && typeof previousSubscription.unsubscribe === "function") {
    try { previousSubscription.unsubscribe(); } catch (_) {}
  }
  window.__QM_NCM_PROGRESS_SUBSCRIPTION__ = null;
  window.__QM_NCM_PROGRESS_STREAM__ = null;
  window.__QM_NCM_NATIVE_PROGRESS__ = null;
  window.__QM_NCM_LAST_REPORT_AT__ = 0;
  window.__QM_NCM_LYRIC_STATE__ = null;
}
window.__QM_NCM_BRIDGE_VERSION__ = bridgeVersion;
const report = (value) => { try { if (typeof window.__qmReport === "function") window.__qmReport(JSON.stringify(value)); } catch (_) {} };
const toSongId = (value) => {
  try {
    const text = String(value == null ? "" : value);
    return /^[1-9][0-9]*$/.test(text) ? text : "";
  } catch (_) {}
  return "";
};
const getWebpackRequire = () => {
  try {
    if (window.__QM_NCM_WEBPACK_REQUIRE__) return window.__QM_NCM_WEBPACK_REQUIRE__;
    const queue = window.webpackJsonp;
    if (!queue || typeof queue.push !== "function") return null;
    let captured = null;
    const moduleId = "__qm_ncm_bridge_capture__";
    queue.push([[], {[moduleId]: function(module, exports, require) { captured = require; }}, [[moduleId]]]);
    if (captured && captured.c) {
      window.__QM_NCM_WEBPACK_REQUIRE__ = captured;
      return captured;
    }
  } catch (_) {}
  return null;
};
const findStore = () => {
  try {
    const cached = window.__QM_NCM_STORE_OWNER__;
    if (cached && typeof cached.getStore === "function") {
      const store = cached.getStore();
      if (store && store.playing) return store;
    }
    const require = getWebpackRequire();
    if (!require || !require.c) return null;
    for (const id of Object.keys(require.c)) {
      const exports = require.c[id] && require.c[id].exports;
      for (const candidate of [exports, exports && exports.default, exports && exports.a]) {
        if (!candidate || typeof candidate.getStore !== "function") continue;
        try {
          const store = candidate.getStore();
          if (store && store.playing) {
            window.__QM_NCM_STORE_OWNER__ = candidate;
            return store;
          }
        } catch (_) {}
      }
    }
  } catch (_) {}
  return null;
};
const findProgressStream = () => {
  try {
    const cached = window.__QM_NCM_CACHED_PROGRESS_STREAM__;
    if (cached && typeof cached.subscribe === "function") return cached;
    const require = getWebpackRequire();
    if (!require || !require.c) return null;
    for (const id of Object.keys(require.c)) {
      const stream = require.c[id] && require.c[id].exports && require.c[id].exports.audioPlayerPlayProgress$;
      if (stream && typeof stream.subscribe === "function") {
        window.__QM_NCM_CACHED_PROGRESS_STREAM__ = stream;
        return stream;
      }
    }
  } catch (_) {}
  return null;
};
const readPlaying = () => {
  try {
    const store = findStore();
    const playing = store && store.playing;
    if (!playing) return null;
    const songId = toSongId(playing.onlineResourceId) || toSongId(playing.resourceTrackId) || toSongId(playing.curTrack && playing.curTrack.id);
    let isPlaying = null;
    if (playing.playingState === 2 || playing.playingState === "playing" || playing.playingState === "play") isPlaying = true;
    else if (playing.playingState === -1 || playing.playingState === 0 || playing.playingState === 1 || playing.playingState === "paused" || playing.playingState === "stopped") isPlaying = false;
    return {songId, isPlaying};
  } catch (_) {}
  return null;
};
const lyricState = () => {
  let state = window.__QM_NCM_LYRIC_STATE__;
  if (!state) {
    state = {songId:"", songChangedAt:0, sawLoading:false, previousContentKey:"", reportedContentKey:"", reportedKey:"", candidateKey:"", candidateCount:0, fetchSongId:"", fetchRequestedAt:0, fetchPending:false, fetchCompleted:false, fetchGeneration:0};
    window.__QM_NCM_LYRIC_STATE__ = state;
  }
  return state;
};
const resetLyricCandidate = (state) => {
  state.candidateKey = "";
  state.candidateCount = 0;
};
const noteLyricSong = (songId, now) => {
  const state = lyricState();
  if (songId && state.songId !== songId) {
    state.songId = songId;
    state.songChangedAt = now;
    state.sawLoading = false;
    state.previousContentKey = state.reportedContentKey;
    state.reportedKey = "";
    state.fetchSongId = "";
    state.fetchRequestedAt = 0;
    state.fetchPending = false;
    state.fetchCompleted = false;
    resetLyricCandidate(state);
  }
  return state;
};
const serializeLyricLines = (lines) => {
  if (!Array.isArray(lines)) return "";
  const output = [];
  let hasText = false;
  for (const line of lines) {
    if (!line || typeof line !== "object" || typeof line.lyric !== "string") continue;
    const seconds = Number(line.time);
    if (!Number.isFinite(seconds) || seconds < 0 || seconds > Number.MAX_SAFE_INTEGER / 1000) continue;
    const totalMs = Math.round(seconds * 1000);
    const minutes = Math.floor(totalMs / 60000);
    const remainder = totalMs - minutes * 60000;
    const wholeSeconds = String(Math.floor(remainder / 1000)).padStart(2, "0");
    const milliseconds = String(remainder % 1000).padStart(3, "0");
    const text = line.lyric.replace(/[\r\n]+/g, " ");
    if (text.trim()) hasText = true;
    output.push(`[${minutes}:${wholeSeconds}.${milliseconds}]${text}`);
  }
  return hasText ? output.join("\n") : "";
};
const finishStoreLyricFetch = (songId, fetchGeneration, success) => {
  const state = lyricState();
  if (state.songId !== songId || state.fetchGeneration !== fetchGeneration) return;
  state.fetchPending = false;
  state.fetchCompleted = success;
  if (success) state.sawLoading = true;
  resetLyricCandidate(state);
};
const requestStoreLyrics = (songId, state, now) => {
  if (state.fetchSongId === songId && now - state.fetchRequestedAt < 30000) return false;
  try {
    const owner = window.__QM_NCM_STORE_OWNER__;
    const dispatch = owner && typeof owner.getDispatch === "function" ? owner.getDispatch() : null;
    if (typeof dispatch !== "function") return false;
    state.fetchSongId = songId;
    state.fetchRequestedAt = now;
    state.fetchPending = true;
    state.fetchCompleted = false;
    state.fetchGeneration += 1;
    const fetchGeneration = state.fetchGeneration;
    const result = dispatch({type:"async:lyric/fetchLyric", payload:{force:true}});
    if (result && typeof result.then === "function")
      result.then(() => finishStoreLyricFetch(songId, fetchGeneration, true), () => finishStoreLyricFetch(songId, fetchGeneration, false));
    else
      finishStoreLyricFetch(songId, fetchGeneration, true);
    return true;
  } catch (_) {
    state.fetchPending = false;
    state.fetchCompleted = false;
  }
  return false;
};
const reportProgress = (force) => {
  try {
    const now = Date.now();
    if (!force && now - (window.__QM_NCM_LAST_REPORT_AT__ || 0) < 750) return;
    const playing = readPlaying();
    const nativeProgress = window.__QM_NCM_NATIVE_PROGRESS__;
    const payload = {kind:"progress", songId:playing ? playing.songId : "", timestamp:now};
    if (nativeProgress && typeof nativeProgress.positionMs === "number" && Number.isFinite(nativeProgress.positionMs)) payload.positionMs = nativeProgress.positionMs;
    if (playing && typeof playing.isPlaying === "boolean") payload.playing = playing.isPlaying;
    if (!payload.songId && payload.positionMs == null && payload.playing == null) return;
    if (payload.songId) noteLyricSong(payload.songId, now);
    window.__QM_NCM_LAST_REPORT_AT__ = now;
    window.__QM_NCM_LAST_PROGRESS__ = payload;
    report(payload);
  } catch (_) {}
};
const reportStoreLyrics = () => {
  try {
    const store = findStore();
    const playing = readPlaying();
    const songId = playing ? playing.songId : "";
    if (!store || !songId) return;
    const now = Date.now();
    const state = noteLyricSong(songId, now);
    const lyric = store["async:lyric"];
    if (state.fetchSongId !== songId) {
      requestStoreLyrics(songId, state, now);
      resetLyricCandidate(state);
      return;
    }
    if (state.fetchPending || !state.fetchCompleted) {
      if (now - state.fetchRequestedAt >= 30000)
        requestStoreLyrics(songId, state, now);
      resetLyricCandidate(state);
      return;
    }
    if (!lyric || (!lyric.isLoading && Array.isArray(lyric.lyricLines) && lyric.lyricLines.length === 0 && lyric.currentUsedLyric === "none" && lyric.displayType === "default"))
      requestStoreLyrics(songId, state, now);
    if (!lyric || lyric.isLoading || !Array.isArray(lyric.lyricLines)) {
      if (lyric && lyric.isLoading) state.sawLoading = true;
      resetLyricCandidate(state);
      return;
    }
    const lrc = serializeLyricLines(lyric.lyricLines);
    if (!lrc) {
      resetLyricCandidate(state);
      return;
    }
    const tlyric = serializeLyricLines(lyric.tlyricLines);
    const rawLyrics = {lrc, tlyric};
    const contentKey = JSON.stringify(rawLyrics);
    if (!state.sawLoading && state.previousContentKey && contentKey === state.previousContentKey && now - state.songChangedAt < 5000) {
      resetLyricCandidate(state);
      return;
    }
    const lyricVersion = `${String(lyric.currentUsedLyric || "")}:${String(lyric.currentUsedLyricVersion == null ? "" : lyric.currentUsedLyricVersion)}`;
    const key = `${songId}\n${lyricVersion}\n${contentKey}`;
    if (state.reportedKey === key) return;
    if (state.candidateKey !== key) {
      state.candidateKey = key;
      state.candidateCount = 1;
      return;
    }
    state.candidateCount += 1;
    if (state.candidateCount < 3) return;
    const payload = {kind:"lyrics", songId, rawLyrics, timestamp:now};
    state.reportedKey = key;
    state.reportedContentKey = contentKey;
    resetLyricCandidate(state);
    window.__QM_NCM_RAW_LYRICS__ = payload;
    report(payload);
  } catch (_) {}
};
const installProgress = () => {
  try {
    const stream = findProgressStream();
    if (!stream || stream === window.__QM_NCM_PROGRESS_STREAM__) return;
    const previous = window.__QM_NCM_PROGRESS_SUBSCRIPTION__;
    if (previous && typeof previous.unsubscribe === "function") previous.unsubscribe();
    window.__QM_NCM_PROGRESS_SUBSCRIPTION__ = stream.subscribe((value) => {
      try {
        const values = Array.isArray(value) ? value : [];
        const seconds = Number(values[1]);
        if (Number.isFinite(seconds) && seconds >= 0) {
          const firstProgress = !window.__QM_NCM_NATIVE_PROGRESS__;
          window.__QM_NCM_NATIVE_PROGRESS__ = {positionMs: Math.max(0, seconds * 1000), timestamp:Date.now()};
          reportProgress(firstProgress);
        }
      } catch (_) {}
    });
    window.__QM_NCM_PROGRESS_STREAM__ = stream;
  } catch (_) {}
};
const installLegacyLyrics = () => {
  try {
    if (typeof window.onProcessLyrics === "function" && !window.onProcessLyrics.__qmWrapped) {
      const original = window.onProcessLyrics;
      const wrapped = function(rawLyrics, songId) {
        try {
          const payload = {kind:"lyrics", songId:toSongId(songId), rawLyrics, timestamp:Date.now()};
          window.__QM_NCM_RAW_LYRICS__ = payload;
          report(payload);
        } catch (_) {}
        return original.apply(this, arguments);
      };
      Object.defineProperty(wrapped, "__qmWrapped", {value:true});
      Object.defineProperty(wrapped, "__qmOriginal", {value:original});
      window.onProcessLyrics = wrapped;
    }
  } catch (_) {}
};
const install = () => { installProgress(); installLegacyLyrics(); reportProgress(false); reportStoreLyrics(); };
install();
if (!window.__QM_NCM_BRIDGE_TIMER__) window.__QM_NCM_BRIDGE_TIMER__ = setInterval(install, 250);
return "installed";
})())JS"
