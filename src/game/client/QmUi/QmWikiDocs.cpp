// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmWikiDocs.h"

#include <engine/shared/localization.h>

#include <iterator>

namespace
{
	// 视频导出说明：内容取自 docs/规格/2026-10-21-视频导出HUD说明文案.md，
	// 面向第一次导出视频的玩家；标题尽量复用弹窗里已有的行标签，避免同一概念两种叫法。
	const char *const s_apVideoExportIntro[] = {
		Localizable("Renders the demo that is currently playing into a video file. It runs as fast as the encoder allows instead of in real time, so a five-minute demo usually finishes in tens of seconds."),
	};
	const char *const s_apOutputResolution[] = {
		Localizable("Sets the size of the finished video; \"Follow window\" uses the current render resolution."),
		Localizable("Anything smaller than the window supersamples: the frame is drawn at window size and scaled down, so 1080p to 720p gives smoother edges. Sizes above the window are ignored."),
	};
	const char *const s_apEncoder[] = {
		Localizable("\"Default (software)\" is x264: CPU only, best quality, slowest."),
		Localizable("Entries with NVENC, AMF, Quick Sync or VideoToolbox use the GPU encoding chip and are several times faster. Only encoders that really work on this machine are listed."),
	};
	const char *const s_apQuality[] = {
		Localizable("0 to 51, lower is clearer and bigger. Leave it empty to use the video encoding CRF from the settings page."),
		Localizable("The scale name depends on the encoder (CRF, CQ, QP, ICQ) but the direction is the same. ProRes fixes quality by profile, so the field is disabled."),
	};
	const char *const s_apChromaSampling[] = {
		Localizable("How much color detail is kept, and the setting that matters most for editing. 4:2:0 is smallest and most compatible, 4:2:2 is the common editing middle ground, 4:4:4 keeps thin lines and small colored text sharp."),
		Localizable("Rendering reads back 8-bit frames, so 10-bit formats add nothing by themselves; the gain comes from chroma density. If the encoder cannot do 4:4:4, the export fails with an error instead of reducing quality silently."),
	};
	const char *const s_apContainer[] = {
		Localizable("MP4 is the most compatible for sharing, MOV is the editing standard and required by ProRes, and MKV suits lossless archives but is poorly supported by editors."),
	};
	const char *const s_apExportPreset[] = {
		Localizable("Sets encoder, container and quality in one step: Daily is H.264 + MP4, High quality is HEVC + MP4 at a smaller size, Editing is all-intra H.264 + MP4 with instant seeking, and Exchange is ProRes 422 HQ + MOV for other editors."),
	};
	const char *const s_apKeyframeInterval[] = {
		Localizable("How often a keyframe is written. Cut points can only land on keyframes: 0 uses the encoder default, 1 makes every frame a keyframe, and 60 leaves one keyframe per second at 60 fps."),
		Localizable("NVIDIA hardware encoders refuse 1 and fall back to 2, with a note in the log; use a software encoder when you need true all-intra."),
	};
	const char *const s_apExportRange[] = {
		Localizable("Optional start and end time in seconds. Leave both empty to export the whole demo."),
	};
	const char *const s_apFrequentlyAsked[] = {
		Localizable("Why is a ProRes file huge but not sharper? ProRes is an exchange format: every editor opens it and scrubbing is smooth. Its 10-bit profile carries the same 8-bit source, so raise chroma sampling for more detail."),
		Localizable("Why does scrubbing lag in my editor? The file uses a normal GOP, so the editor decodes from the previous keyframe. Re-export with the Editing preset."),
		Localizable("Why will some players not open a 4:4:4 file? H.264 4:4:4 uses the High 4:4:4 Predictive profile, which some hardware decoders and phones lack. Use 4:2:0 for wide compatibility."),
	};

	const SWikiSection s_aVideoExportSections[] = {
		{Localizable("Video export"), s_apVideoExportIntro, (int)std::size(s_apVideoExportIntro)},
		{Localizable("Output resolution"), s_apOutputResolution, (int)std::size(s_apOutputResolution)},
		{Localizable("Encoder"), s_apEncoder, (int)std::size(s_apEncoder)},
		{Localizable("Quality"), s_apQuality, (int)std::size(s_apQuality)},
		{Localizable("Chroma sampling"), s_apChromaSampling, (int)std::size(s_apChromaSampling)},
		{Localizable("Container"), s_apContainer, (int)std::size(s_apContainer)},
		{Localizable("Export preset"), s_apExportPreset, (int)std::size(s_apExportPreset)},
		{Localizable("Keyframe interval (GOP)"), s_apKeyframeInterval, (int)std::size(s_apKeyframeInterval)},
		{Localizable("Export range"), s_apExportRange, (int)std::size(s_apExportRange)},
		{Localizable("Frequently asked questions"), s_apFrequentlyAsked, (int)std::size(s_apFrequentlyAsked)},
	};

	const SWikiDocument s_VideoExportDocument = {
		"demo:video_export",
		Localizable("Video export"),
		s_aVideoExportSections,
		(int)std::size(s_aVideoExportSections),
	};
}

const SWikiDocument &qm_wiki_docs::VideoExport()
{
	return s_VideoExportDocument;
}
