#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VIDEO_PRESET_LABELS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VIDEO_PRESET_LABELS_H

#include <game/localization.h>

#include <algorithm>

// libx264 编码参数 UI 用的纯映射函数。
// 上游 preset 名称序列固定（0=ultrafast … 9=placebo），与 config_variables.h 中
// cl_video_crf（默认 18，0=最好质量、51=最差）和 cl_video_preset（默认 5=medium）对应。
namespace qm_video_encode
{
	constexpr int PRESET_COUNT = 10;
	constexpr int PRESET_RECOMMENDED = 5; // medium
	constexpr int CRF_DEFAULT = 18;
	constexpr int FPS_DEFAULT = 60; // cl_video_recorder_fps 默认值
	// 弹窗内录制帧率滑条的范围：配置本身允许 1-1000，这里只提供常用区间。
	constexpr int FPS_SLIDER_MIN = 30;
	constexpr int FPS_SLIDER_MAX = 240;

	// 返回 0-9 对应的上游 libx264 preset 英文名称，越界输入钳制到 0-9。
	inline const char *QmVideoPresetName(int Preset)
	{
		constexpr const char *const apNames[PRESET_COUNT] = {
			"ultrafast",
			"superfast",
			"veryfast",
			"faster",
			"fast",
			"medium",
			"slow",
			"slower",
			"veryslow",
			"placebo",
		};
		return apNames[std::clamp(Preset, 0, PRESET_COUNT - 1)];
	}

	// 返回各 preset 的一句话说明（已本地化）。字面量位于 Localize 调用内，
	// 供 i18n 提取脚本识别；未加载语言库时回退为英文源键，单测可直接断言键值。
	inline const char *QmVideoPresetHint(int Preset)
	{
		switch(std::clamp(Preset, 0, PRESET_COUNT - 1))
		{
		case 0: return Localize("Fastest encode, largest files");
		case 1: return Localize("Very fast encode");
		case 2: return Localize("Fast encode");
		case 3: return Localize("Fairly fast encode");
		case 4: return Localize("Slightly fast encode");
		case 5: return Localize("Balanced (recommended)");
		case 6: return Localize("Better compression, slower");
		case 7: return Localize("High quality, slow");
		case 8: return Localize("Very high quality, very slow");
		default: return Localize("Extremely slow, for testing only");
		}
	}

	// CRF 档位文案：0-15 极高、16-23 高（推荐）、24-31 中、32+ 低。
	inline const char *QmVideoCrfTierLabel(int Crf)
	{
		if(Crf <= 15)
			return Localize("Very high");
		if(Crf <= 23)
			return Localize("High (recommended)");
		if(Crf <= 31)
			return Localize("Medium");
		return Localize("Low");
	}
}

#endif
