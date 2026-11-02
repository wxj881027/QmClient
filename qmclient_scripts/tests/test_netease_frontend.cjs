const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

// 与 BuildInstallHookScript() 共用原始字符串；实际执行注入脚本，不检查源码片段。
const scriptPath = path.join(__dirname, '../../src/qm/hooks/netease/qm_netease_frontend_script.inc');
const literal = fs.readFileSync(scriptPath, 'utf8').trim();
if (!literal.startsWith('R"JS(') || !literal.endsWith(')JS"')) {
    throw new Error('网易云注入脚本必须是单个 C++ 原始字符串');
}
const script = literal.slice('R"JS('.length, -')JS"'.length);

function lyricFixture() {
    return {
        isLoading: false,
        currentUsedLyric: 'lrc',
        currentUsedLyricVersion: 1,
        lyricLines: [{ time: 1.25, lyric: 'original one' }, { time: 3, lyric: 'original two' }],
        tlyricLines: [{ time: 1.25, lyric: '第一句译文' }, { time: 3, lyric: '第二句译文' }],
    };
}

function createBridge(lyric = lyricFixture()) {
    let now = 100000;
    let nextTimer = 0;
    let progressCallback;
    let subscriptions = 0;
    const timers = new Map();
    const reports = [];
    const fetches = [];
    const store = { playing: { onlineResourceId: '42', playingState: 2 }, 'async:lyric': lyric };
    const owner = {
        getStore: () => store,
        getDispatch: () => action => {
            const fetch = { action };
            fetches.push(fetch);
            return { then(resolve, reject) { Object.assign(fetch, { resolve, reject }); } };
        },
    };
    const require = { c: {
        store: { exports: owner },
        player: { exports: { audioPlayerPlayProgress$: {
            subscribe(callback) {
                ++subscriptions;
                progressCallback = callback;
                return { unsubscribe() { --subscriptions; } };
            },
        } } },
    } };
    const window = {
        __qmReport: json => reports.push(JSON.parse(json)),
        webpackJsonp: { push([, modules]) { Object.values(modules)[0]({}, {}, require); } },
    };
    const context = vm.createContext({
        window,
        Date: { now: () => now },
        setInterval: callback => { timers.set(++nextTimer, callback); return nextTimer; },
        clearInterval: id => timers.delete(id),
    });
    const install = () => vm.runInContext(script, context, { filename: scriptPath, timeout: 1000 });
    install();
    return {
        store, reports, fetches, install,
        lyrics: () => reports.filter(report => report.kind === 'lyrics'),
        complete: (index = fetches.length - 1) => fetches[index].resolve(),
        reject: (index = fetches.length - 1) => fetches[index].reject(),
        advance(milliseconds = 250) {
            now += milliseconds;
            for (const callback of [...timers.values()]) callback();
        },
        tick(count = 1) { for (let index = 0; index < count; ++index) this.advance(); },
        emitProgress: seconds => progressCallback([null, seconds]),
        timerCount: () => timers.size,
        subscriptionCount: () => subscriptions,
    };
}

test('现代网易云同时上报原文和自带译文，不受网易云显示开关影响', () => {
    const lyric = lyricFixture();
    lyric.translationEnable = false;
    const bridge = createBridge(lyric);
    bridge.complete();
    bridge.tick(3);
    assert.equal(bridge.lyrics().length, 1);
    assert.equal(bridge.lyrics()[0].songId, '42');
    assert.deepEqual(bridge.lyrics()[0].rawLyrics, {
        lrc: '[0:01.250]original one\n[0:03.000]original two',
        tlyric: '[0:01.250]第一句译文\n[0:03.000]第二句译文',
    });
});

test('缺少或全空的翻译轨保留原文，不把罗马音当作译文', () => {
    for (const translations of [undefined, [], [{ time: 1.25, lyric: ' ' }]]) {
        const lyric = lyricFixture();
        lyric.tlyricLines = translations;
        lyric.romaLyricLines = [{ time: 1.25, lyric: 'romanization' }];
        const bridge = createBridge(lyric);
        bridge.complete();
        bridge.tick(3);
        assert.equal(bridge.lyrics().length, 1);
        assert.equal(bridge.lyrics()[0].rawLyrics.lrc, '[0:01.250]original one\n[0:03.000]original two');
        assert.equal(bridge.lyrics()[0].rawLyrics.tlyric, '');
    }
});

test('译文晚于原文到达时重新上报，不要求原文版本变化', () => {
    const lyric = lyricFixture();
    const translations = lyric.tlyricLines;
    lyric.tlyricLines = [];
    const bridge = createBridge(lyric);
    bridge.complete();
    bridge.tick(3);
    assert.equal(bridge.lyrics()[0].rawLyrics.tlyric, '');
    lyric.tlyricLines = translations;
    bridge.tick(2);
    assert.equal(bridge.lyrics().length, 1);
    bridge.tick();
    assert.equal(bridge.lyrics().length, 2);
    assert.equal(bridge.lyrics()[1].rawLyrics.tlyric, '[0:01.250]第一句译文\n[0:03.000]第二句译文');
});

test('仅修订译文会重新上报，稳定内容不重复发布', () => {
    const lyric = lyricFixture();
    const bridge = createBridge(lyric);
    bridge.complete();
    bridge.tick(6);
    assert.equal(bridge.lyrics().length, 1);
    lyric.tlyricLines[0].lyric = '修订译文';
    bridge.tick(3);
    assert.equal(bridge.lyrics().length, 2);
    assert.equal(bridge.lyrics()[1].rawLyrics.tlyric, '[0:01.250]修订译文\n[0:03.000]第二句译文');
    bridge.tick(6);
    assert.equal(bridge.lyrics().length, 2);
});

test('原文和译文都跳过无效时间并将句内换行序列化为空格', () => {
    const lyric = lyricFixture();
    lyric.lyricLines = [{ time: -1, lyric: 'invalid' }, { time: 1.25, lyric: 'one\ntwo' }];
    lyric.tlyricLines = [{ time: NaN, lyric: 'invalid' }, { time: 1.25, lyric: '一\r\n二' }];
    const bridge = createBridge(lyric);
    bridge.complete();
    bridge.tick(3);
    assert.deepEqual(bridge.lyrics()[0].rawLyrics, { lrc: '[0:01.250]one two', tlyric: '[0:01.250]一 二' });
});

test('当前歌曲刷新完成前不发布缓存歌词', () => {
    const bridge = createBridge();
    bridge.tick(6);
    assert.equal(bridge.lyrics().length, 0);
    assert.equal(bridge.fetches.length, 1);
    assert.equal(bridge.fetches[0].action.type, 'async:lyric/fetchLyric');
    assert.equal(bridge.fetches[0].action.payload.force, true);
    bridge.complete();
    bridge.tick(2);
    assert.equal(bridge.lyrics().length, 0);
    bridge.tick();
    assert.equal(bridge.lyrics().length, 1);
});

test('切歌后旧请求完成不会提前发布旧译文', () => {
    const bridge = createBridge();
    bridge.store.playing.onlineResourceId = '43';
    bridge.store['async:lyric'].isLoading = true;
    bridge.tick();
    assert.equal(bridge.fetches.length, 2);
    bridge.complete(0);
    bridge.tick(3);
    assert.equal(bridge.lyrics().length, 0);
    bridge.store['async:lyric'] = {
        ...lyricFixture(),
        lyricLines: [{ time: 2, lyric: 'new song' }],
        tlyricLines: [{ time: 2, lyric: '新歌译文' }],
    };
    bridge.complete(1);
    bridge.tick(3);
    assert.equal(bridge.lyrics().length, 1);
    assert.equal(bridge.lyrics()[0].songId, '43');
    assert.deepEqual(bridge.lyrics()[0].rawLyrics, { lrc: '[0:02.000]new song', tlyric: '[0:02.000]新歌译文' });
});

test('歌词刷新失败或超时后可以重试并恢复双语上报', () => {
    for (const fail of [false, true]) {
        const bridge = createBridge();
        if (fail) bridge.reject();
        bridge.advance(30000);
        assert.equal(bridge.fetches.length, 2);
        assert.equal(bridge.lyrics().length, 0);
        bridge.complete(1);
        bridge.tick(3);
        assert.equal(bridge.lyrics().length, 1);
        assert.equal(bridge.lyrics()[0].rawLyrics.tlyric, '[0:01.250]第一句译文\n[0:03.000]第二句译文');
    }
});

test('重复注入复用定时器和进度订阅，保留真实进度与暂停状态', () => {
    const bridge = createBridge();
    bridge.install();
    assert.equal(bridge.timerCount(), 1);
    assert.equal(bridge.subscriptionCount(), 1);
    assert.equal(bridge.fetches.length, 1);
    bridge.store.playing.playingState = 1;
    bridge.emitProgress(1.25);
    const progress = bridge.reports.filter(report => report.kind === 'progress').at(-1);
    assert.equal(progress.positionMs, 1250);
    assert.equal(progress.playing, false);
});
