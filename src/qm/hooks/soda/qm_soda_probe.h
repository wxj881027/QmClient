#ifndef QM_HOOKS_SODA_QM_SODA_PROBE_H
#define QM_HOOKS_SODA_QM_SODA_PROBE_H

#include <string>
#include <string_view>

// 汽水音乐(SodaMusic.exe, Electron)播放态提取的纯函数部分。
// 数据链路(参考开源实现 VTB-LINK/Metabox-Nexus-PlayerCap,真机验证):
//   1. 主进程 Node inspector(9229)激活(watchdog,见 qm_soda_watchdog)。
//   2. 经主进程 webContents.executeJavaScript 桥进 rendererMain 主窗口。
//   3. patch MessagePort.postMessage 抓 transport 通道 port1。
//   4. sendTransport({type:'method.invoke', methodName:'get', arguments:['player']})。
//   5. 截 method.return 归一化返回 {mediaId, name, artists, album, coverUrl,
//      isPlaying, progressSeconds, durationSeconds, lyricType, lyricContent, translationLrc}。
// 本模块只含脚本构建与结果解析,网络/线程见 helper 与 watchdog。
namespace QmSodaProbe
{
	// 幂等的 rendererMain 探针脚本(纯 ASCII;由 bridge 脚本 base64 编码后注入)。
	std::string BuildInnerProbeScript();

	// 主进程桥表达式:找 main.html webContents,重申 setBackgroundThrottling(false),
	// executeJavaScript(innerProbe) 并把节流状态带回。
	std::string BuildBridgeExpression();

	// 解析提取结果 JSON(ExtractionData 归一化格式)为播放态快照。
	struct SPlaybackSnapshot
	{
		bool m_Ok = false;
		bool m_IsPlaying = false;
		bool m_IsLoading = false;
		double m_ProgressSeconds = 0;
		double m_DurationSeconds = 0;
		std::string m_MediaId;
		std::string m_Name;
		std::string m_Artist; // 多个歌手以 ", " 连接
		std::string m_Album;
		std::string m_CoverUrl;
		std::string m_LyricType; // "krc" / "lrc" / ""
		std::string m_LyricContent; // 已解密的明文 KRC / LRC
		std::string m_TranslationLrc; // 中文翻译轨(独立 LRC),可为空
		bool m_Throttled = false;
		std::string m_Error;
	};

	// 解析提取结果 JSON。失败时 m_Ok=false 且 m_Error 有原因。
	bool ParseExtractionJson(std::string_view Json, SPlaybackSnapshot *pOut);
} // namespace QmSodaProbe

#endif
