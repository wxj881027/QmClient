#ifndef GAME_CLIENT_QMUI_ANTIPINGPLAYERSSETTINGS_H
#define GAME_CLIENT_QMUI_ANTIPINGPLAYERSSETTINGS_H

// cl_antiping_players 的实际取值：
//   0 = 关闭；1 = 预测所有玩家；2 = 只预测交互玩家（含分身）；
//   3 = 在 2 的基础上，冻结玩家也始终预测。
// 设置页把它拆成「Off / All / Interact」三档单选，冻结玩家是 Interact 的从属开关，
// 因此三档单选必须把 3 折叠成 Interact 展示，写回时再保留 3。
namespace QmAntiPingPlayersSettings
{
	inline constexpr int OFF = 0;
	inline constexpr int ALL = 1;
	inline constexpr int INTERACT = 2;
	inline constexpr int INTERACT_FROZEN = 3;

	// 单选展示值：3 与 2 是同一档，非法值钳到最近的有效档。
	inline int BaseChoice(const int Value)
	{
		if(Value >= INTERACT)
			return INTERACT;
		if(Value <= OFF)
			return OFF;
		return ALL;
	}

	// 用户点了三档单选后写回配置：选 Interact 时保留原来的冻结开关状态，
	// 因为点击当前已选中的档不会产生变更事件，只有从别的档切回来才需要复位。
	inline int ApplyBaseChoice(const int Current, const int Choice)
	{
		if(Choice == INTERACT)
			return Current == INTERACT_FROZEN ? INTERACT_FROZEN : INTERACT;
		return Choice == ALL ? ALL : OFF;
	}

	// 冻结玩家开关只在交互档下有意义。
	inline bool ShowsFrozenOption(const int Value)
	{
		return Value >= INTERACT;
	}

	inline bool FrozenOptionEnabled(const int Value)
	{
		return Value == INTERACT_FROZEN;
	}

	// 冻结开关写回：开启落 3，关闭落 2；非交互档不接受该开关。
	inline int ApplyFrozenOption(const int Current, const bool Enabled)
	{
		if(!ShowsFrozenOption(Current))
			return Current;
		return Enabled ? INTERACT_FROZEN : INTERACT;
	}
} // namespace QmAntiPingPlayersSettings

#endif
