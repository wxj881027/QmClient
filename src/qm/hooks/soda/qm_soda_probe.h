#ifndef QM_HOOKS_SODA_QM_SODA_PROBE_H
#define QM_HOOKS_SODA_QM_SODA_PROBE_H

#include <cstdint>
#include <new>
#include <stdexcept>
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

	// 优先选 Node target，只接受同一发现端口上的回环 WebSocket。
	std::string PickNodeTarget(std::string_view Json, uint16_t Port);

	// 解析提取结果 JSON(ExtractionData 归一化格式)为播放态快照。
	struct SPlaybackSnapshot
	{
		bool m_Ok = false;
		bool m_IsPlaying = false;
		bool m_IsLoading = false;
		double m_ProgressSeconds = -1;
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

		// 无有效进度返回 -1。未知时长以协议已有的 0 表示。
		int64_t PositionMs() const;
		int64_t DurationMs() const;
	};

	// 解析提取结果 JSON。失败时 m_Ok=false 且 m_Error 有原因。
	bool ParseExtractionJson(std::string_view Json, SPlaybackSnapshot *pOut);

	class CLyricFilePublication
	{
		static constexpr uint64_t WRITE_RETRY_MS = 1000;
		std::string m_MediaId;
		std::string m_Path;
		uint64_t m_Generation = 0;
		uint64_t m_NextWriteTick = 0;

	public:
		void Reset() noexcept { *this = {}; }
		uint64_t Generation() const { return m_Generation; }
		const std::string &Path() const { return m_Path; }

		template<typename FWrite>
		void Update(const SPlaybackSnapshot &Snapshot, uint64_t Now, const FWrite &Write)
		{
			if(Snapshot.m_MediaId.empty())
				return;
			if(Snapshot.m_MediaId != m_MediaId)
			{
				m_MediaId = Snapshot.m_MediaId;
				if(++m_Generation == 0)
					m_Generation = 1;
				m_Path.clear();
				m_NextWriteTick = 0;
			}
			if(Snapshot.m_LyricContent.empty() || !m_Path.empty() || Now < m_NextWriteTick)
				return;

			// 歌曲身份可以先发布。歌词到齐后，只有写盘成功才记住路径。
			m_NextWriteTick = Now + WRITE_RETRY_MS;
			try
			{
				m_Path = Write(Snapshot);
			}
			catch(const std::bad_alloc &)
			{
			}
			catch(const std::length_error &)
			{
			}
		}
	};
} // namespace QmSodaProbe

#endif
