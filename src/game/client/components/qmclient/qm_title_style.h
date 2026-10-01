// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_TITLE_STYLE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_TITLE_STYLE_H

#include <base/color.h>

inline bool QmTitleVisibleForLocalConnection(int ClientId, int MainClientId, int DummyClientId, bool ShowMain, bool ShowDummy)
{
	if(ClientId < 0)
		return false;
	if(ClientId == MainClientId)
		return ShowMain;
	if(ClientId == DummyClientId)
		return ShowDummy;
	return true;
}

// 头衔动态风格：逐字节复刻 Calamity Mod 2.2.2 的稀有度配色与插值。
// 色值出处与公式原文见 docs/qmclient/title_style_spec.md。
//
// 与 Calamity 的对应关系：
//   SinSwap    ← CalamityUtils.ColorSwap
//   MultiLerp  ← CalamityUtils.MulticolorLerp
//   PhaseCycle ← ExoticRainbow / Eternity / Earth 的逐字符像素相位模板
//   Piecewise  ← FlamsteedRing 的分段时序

enum class EQmTitleStyleMode : int
{
	Static, // 恒定单色
	SinSwap, // 正弦往返，往返一次耗时 m_PeriodSec
	MultiLerp, // m_PeriodSec 秒走完整个色环
	PhaseCycle, // 每 m_PeriodSec 秒推进一个色标，秒内插值，可带逐字符像素相位
	Piecewise, // 固定分段时序（0.6 / 0.2 / 0.2 秒）
};

// ExoticRainbow 的插值因子在源码里被 MathF.Round 量化到 0/1，属于硬切换。
// 默认档位使用 Smooth，Hard 保留用于逐字节还原。
enum class EQmTitleInterpolation : int
{
	Smooth,
	Hard,
};

struct SQmTitleStyle
{
	const char *m_pId;
	const char *m_pLabel; // 英文显示名，界面文案的本地化在设置页处理
	EQmTitleStyleMode m_Mode;
	// 源码原始插值方式（ExoticRainbow 为 Hard）。默认渲染档位见 QmTitleStyleSample。
	EQmTitleInterpolation m_Interpolation;
	const unsigned int *m_pColors; // 0xRRGGBB 色标
	int m_ColorCount;
	float m_PeriodSec;
	float m_TimeScale; // 时间系数
	float m_PhasePerPx; // 位置系数，0 表示整行同步（无空间相位）
};

// 逐字符波浪浮动参数。效果与 Calamity 的 Wavy 文本效果同构（见 UI/DialogueDisplay/TextEffects/Wavy.cs），
// 但 QmClient 把它作为与稀有度配色正交的独立层，默认关闭后即可还原静止文字。
struct SQmTitleBobStyle
{
	float m_Amplitude; // 垂直幅度（像素），0 表示关闭
	float m_WaveLength; // 波长（像素）
	float m_Speed; // 相位角速度（弧度/秒），与 Wavy 的 freq 同单位
	// 偏移量化步长（像素）。0 表示亚像素（默认）：位移随帧连续变化，观感平滑；
	// 1 表示吸附整数像素：轮廓更锐利，但小幅度下会长时间停在同一像素再跳变。
	float m_QuantizeStep;
};

// 波浪浮动偏移（像素）。相位按像素 X 递进，因此相邻字符的偏移不同，呈现从左向右扫过的波纹。
// 结果量化到整数像素：亚像素位置会让像素字体发虚，与「保持清晰锐利轮廓」的目标冲突。
float QmTitleStyleBobOffset(const SQmTitleBobStyle &Bob, float TimeSec, float PixelX);

// 聊天每行上下各需预留的浮动空间，按屏幕像素向上对齐，避免行距取整后压住波峰。
float QmTitleStyleBobPadding(const SQmTitleBobStyle &Bob, float PixelSize);

// 有效相位系数：Override > 0 时覆盖风格自带值。Calamity 只有 ExoticRainbow 系列自带非零系数，
// 其余风格整行同步变色；要让短头衔也看出光带，需要调用方提高这个值。
float QmTitleStyleEffectivePhasePerPx(const SQmTitleStyle &Style, float Override);

// 动画相位基准：把本地时间对齐到服务端时间，并取模到一小时。
//
// 取模是必需的：相位最终以 float 传给采样器，而 Unix 时间戳量级（1e9）在 float 下 ULP 约 64 秒，
// 相位会完全失效。所有风格周期（1/2/3/4/6 秒）都能整除 3600，因此取模不会在边界产生跳变。
// 各客户端都以服务端时间为基准，取模结果一致，从而「所有人看到同一玩家同一时刻的效果相同」。
double QmTitleAnimationTime(double LocalTime, double ServerTimeOffset, bool OffsetValid);

// 服务端时间偏移的指数平滑：单次估算受网络延迟与调度抖动影响有几十毫秒起伏，
// 直接采用会让相位一跳一跳。首次估算直接采纳。
double QmTitleUpdateServerTimeOffset(double Current, bool CurrentValid, double Measured);

// 按 id 查找风格，未找到返回 nullptr。
const SQmTitleStyle *QmTitleStyleById(const char *pId);
// 按序号取风格，供设置页列表与随机抽取使用，Index 需落在 [0, QmTitleStyleCount())。
const SQmTitleStyle *QmTitleStyleByIndex(int Index);
int QmTitleStyleCount();

// 采样颜色。TimeSec 为统一的服务端同步时间，PixelX 为字符左边缘相对行首的像素偏移。
ColorRGBA QmTitleStyleSample(const SQmTitleStyle &Style, float TimeSec, float PixelX);
// 同上，但按调用方指定值覆盖插值方式（用于「默认平滑、可选硬切换」）。
ColorRGBA QmTitleStyleSampleWithInterpolation(const SQmTitleStyle &Style, float TimeSec, float PixelX, EQmTitleInterpolation Interpolation);
// 该风格的源码原始插值方式，供设置项展示默认档位。
EQmTitleInterpolation QmTitleStyleSourceInterpolation(const SQmTitleStyle &Style);

// 按 0xRRGGBB 构造不透明颜色，仅供测试与调试使用。
ColorRGBA QmTitleStyleColorFromHex(unsigned int Hex);

#endif
