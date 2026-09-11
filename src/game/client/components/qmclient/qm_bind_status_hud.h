// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_BIND_STATUS_HUD_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_BIND_STATUS_HUD_H

// 内置四项状态行的语义配色档位（关闭彩虹色 HUD 后生效）
enum class EQmBindStatusTone
{
	NONE = 0, // 无分类：沿用默认文字色
	OK, // 开 / 正常
	WARNING, // 警示：Reset Self / HDF / Custom
	DANGER, // 关 / DF
};

// 内置四项状态行标识（与 hud.cpp 内置四项一一对应）
enum class EQmBindStatusLine
{
	KEY_STICKING = 0, // 卡键 cl_dummy_resetonswitch：0=On 1=Off 2=Reset Self
	HAMMER, // 锤子 qm_deepfly_mode：0=Normal 1=DF 2=HDF 3=Custom
	DUMMY_CONTROL, // 分控 cl_dummy_control：0=关 非零=开
	DUMMY_COPY, // 同步 cl_dummy_copy_moves：0=关 非零=开
};

// 由内置状态行的当前值推导语义配色；取值没有对应状态时返回 NONE（纯函数，可单测）
EQmBindStatusTone QmResolveBuiltinBindStatusTone(EQmBindStatusLine Line, int Value);

#endif
