#include "explanations.h"

#include <base/system.h>

#include <game/mapitems.h>

// DDNet entity explanations by Lady Saavik
// DDNet entity Translation by 栖梦
// TODO: Add other entities' tiles' explanations and improve new ones

// Tile Numbers For Explanations - TODO: Add/Improve tiles and explanations
enum
{
	TILE_PUB_AIR = 0,
	TILE_PUB_HOOKABLE = 1,
	TILE_PUB_DEATH = 2,
	TILE_PUB_UNHOOKABLE = 3,

	TILE_PUB_CREDITS1 = 140,
	TILE_PUB_CREDITS2 = 141,
	TILE_PUB_CREDITS3 = 142,
	TILE_PUB_CREDITS4 = 143,
	TILE_PUB_CREDITS5 = 156,
	TILE_PUB_CREDITS6 = 157,
	TILE_PUB_CREDITS7 = 158,
	TILE_PUB_CREDITS8 = 159,

	TILE_PUB_ENTITIES_OFF1 = 190,
	TILE_PUB_ENTITIES_OFF2 = 191,
};

enum
{
	TILE_FNG_SPIKE_GOLD = 7,
	TILE_FNG_SPIKE_NORMAL = 8,
	TILE_FNG_SPIKE_RED = 9,
	TILE_FNG_SPIKE_BLUE = 10,
	TILE_FNG_SCORE_RED = 11,
	TILE_FNG_SCORE_BLUE = 12,

	TILE_FNG_SPIKE_GREEN = 14,
	TILE_FNG_SPIKE_PURPLE = 15,

	TILE_FNG_SPAWN = 192,
	TILE_FNG_SPAWN_RED = 193,
	TILE_FNG_SPAWN_BLUE = 194,
	TILE_FNG_FLAG_RED = 195,
	TILE_FNG_FLAG_BLUE = 196,
	TILE_FNG_SHIELD = 197,
	TILE_FNG_HEART = 198,
	TILE_FNG_SHOTGUN = 199,
	TILE_FNG_GRENADE = 200,
	TILE_FNG_NINJA = 201,
	TILE_FNG_LASER = 202,

	TILE_FNG_SPIKE_OLD1 = 208,
	TILE_FNG_SPIKE_OLD2 = 209,
	TILE_FNG_SPIKE_OLD3 = 210,
};

enum
{
	TILE_VANILLA_SPAWN = 192,
	TILE_VANILLA_SPAWN_RED = 193,
	TILE_VANILLA_SPAWN_BLUE = 194,
	TILE_VANILLA_FLAG_RED = 195,
	TILE_VANILLA_FLAG_BLUE = 196,
	TILE_VANILLA_SHIELD = 197,
	TILE_VANILLA_HEART = 198,
	TILE_VANILLA_SHOTGUN = 199,
	TILE_VANILLA_GRENADE = 200,
	TILE_VANILLA_NINJA = 201,
	TILE_VANILLA_LASER = 202,
};

const char *CExplanations::ExplainDDNet(int Tile, int Layer)
{
	switch(Tile)
	{
	case TILE_AIR:
		return "空白: 可用作橡皮擦.";
	case TILE_SOLID:
		if(Layer == LAYER_GAME)
			return "可钩: 可以钩住并与其碰撞.";
		break;
	case TILE_DEATH:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "杀死: 杀死tee.";
		break;
	case TILE_NOHOOK:
		if(Layer == LAYER_GAME)
			return "不可钩: 无法钩住,但可以与其碰撞.";
		break;
	case TILE_NOLASER:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "激光阻挡器: 不让拖拽和旋转激光以及炮塔穿过它到达tee.";
		break;
	case TILE_THROUGH_CUT:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "钩穿: 新钩穿的快捷方式.";
		break;
	case TILE_THROUGH_ALL:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "钩穿: 与碰撞图块结合是新钩穿,否则从所有方向阻止钩子.";
		break;
	case TILE_THROUGH_DIR:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "钩穿: 与碰撞图块结合是新钩穿,否则从一个方向阻止钩子.";
		break;
	case TILE_THROUGH:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "钩穿: 与(不)可钩图块结合,允许钩穿墙壁.";
		break;
	case TILE_JUMP:
		if(Layer == LAYER_SWITCH)
			return "跳跃: 设置定义的跳跃次数(默认为2).";
		break;
	case TILE_FREEZE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "冻结: 冻结tee 3秒.";
		if(Layer == LAYER_SWITCH)
			return "冻结: 冻结tee指定的秒数.";
		break;
	case TILE_UNFREEZE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "解冻: 立即解冻tee.";
		break;
	case TILE_TELEINEVIL:
		if(Layer == LAYER_TELE)
			return "红色传送: 掉入此图块后,tee会出现在相同编号的TO上.速度和钩子被重置.";
		break;
	case TILE_DFREEZE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "深度冻结: 永久冻结.只有深度解冻图块可以取消此效果.";
		break;
	case TILE_DUNFREEZE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "深度解冻: 移除深度冻结效果.";
		break;
	case TILE_LFREEZE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "实时冻结: 实时冻结的tee无法移动或跳跃,但仍可使用钩子和武器.";
		break;
	case TILE_LUNFREEZE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "实时解冻: 移除实时冻结效果.";
		break;
	case TILE_TELEINWEAPON:
		if(Layer == LAYER_TELE)
			return "武器传送: 将射入其中的子弹传送到传送目标,从那里射出.方向、角度和长度保持不变.";
		break;
	case TILE_TELEINHOOK:
		if(Layer == LAYER_TELE)
			return "钩子传送: 将进入其中的钩子传送到传送目标,从那里伸出.方向、角度和长度保持不变.";
		break;
	case TILE_WALLJUMP:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "墙跳: 放置在墙旁边,在玩家贴墙下落的途中补充跳跃";
		break;
	case TILE_EHOOK_ENABLE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "无限钩: 激活无限钩子.";
		break;
	case TILE_EHOOK_DISABLE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "无限钩关闭: 禁用无限钩子.";
		break;
	case TILE_HIT_ENABLE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "攻击其他人: 你可以攻击其他人.";
		if(Layer == LAYER_SWITCH)
			return "攻击其他人: 你可以为单个武器激活攻击其他人,使用延迟编号选择哪个.";
		break;
	case TILE_HIT_DISABLE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "攻击其他人: 你不能攻击其他人.";
		if(Layer == LAYER_SWITCH)
			return "攻击其他人: 你可以为单个武器禁用攻击其他人,使用延迟编号选择哪个.";
		break;
	case TILE_SOLO_ENABLE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "单人: 你现在在单人区域.";
		break;
	case TILE_SOLO_DISABLE: // also TILE_SWITCHTIMEDOPEN
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "单人: 你现在离开了单人区域.";
		if(Layer == LAYER_SWITCH)
			return "时间开关: 在设定的秒数内激活相同编号的开关(例如关门).";
		break;
	case TILE_SWITCHTIMEDCLOSE:
		if(Layer == LAYER_SWITCH)
			return "时间开关: 在设定的秒数内禁用相同编号的开关(例如开门).";
		break;
	case TILE_SWITCHOPEN:
		if(Layer == LAYER_SWITCH)
			return "开关: 激活相同编号的开关(例如关门).";
		break;
	case TILE_SWITCHCLOSE:
		if(Layer == LAYER_SWITCH)
			return "开关: 禁用相同编号的开关(例如开门).";
		break;
	case TILE_TELEIN:
		if(Layer == LAYER_TELE)
			return "蓝色传送: 掉入此图块后,tee会出现在相同编号的TO上.速度和钩子保持不变.";
		break;
	case TILE_TELEOUT:
		if(Layer == LAYER_TELE)
			return "传送目标: 相同编号的FROM、武器和钩子传送的目标图块.";
		break;
	case TILE_SPEED_BOOST_OLD:
		if(Layer == LAYER_SPEEDUP)
			return "旧加速: 给tee定义的速度.箭头显示方向和角度.已废弃.";
		break;
	case TILE_TELECHECK: // also TILE_SPEED_BOOST
		if(Layer == LAYER_TELE)
			return "检查点传送: 接触此图块后,任何CFRM都会将你传送到相同编号的CTO.";
		if(Layer == LAYER_SPEEDUP)
			return "加速: 给tee定义的速度.箭头显示方向和角度.";
		break;
	case TILE_TELECHECKOUT:
		if(Layer == LAYER_TELE)
			return "检查点传送目标: 在接触相同编号的传送检查点并掉入CFROM传送后,tee会出现在这里.";
		break;
	case TILE_TELECHECKIN:
		if(Layer == LAYER_TELE)
			return "蓝色检查点传送: 将tee传送到与最后接触的传送检查点相同编号的CTO.速度和钩子保持不变.";
		break;
	case TILE_REFILL_JUMPS:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "补充跳跃: 恢复所有跳跃次数.";
		break;
	case TILE_START:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "起点: 开始计算你的跑图时间.";
		break;
	case TILE_FINISH:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "终点: 跑图结束.";
		break;
	case TILE_STOP:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "阻挡器: 你可以钩住并射穿它.你不能逆着箭头方向通过它.";
		break;
	case TILE_STOPS:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "阻挡器: 你可以钩住并射穿它.你不能逆着箭头方向通过它.";
		break;
	case TILE_STOPA:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "阻挡器: 你可以钩住并射穿它.你不能通过它.";
		break;
	case TILE_TELECHECKINEVIL:
		if(Layer == LAYER_TELE)
			return "红色检查点传送: 将tee传送到与最后接触的传送检查点相同编号的CTO.速度和钩子被重置.";
		break;
	case TILE_CP:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "速度器: 使武器、盾牌、冻结心和旋转激光缓慢移动.";
		break;
	case TILE_CP_F:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "速度器: 使武器、盾牌、冻结心和旋转激光快速移动.";
		break;
	case TILE_TUNE:
		if(Layer == LAYER_TUNE)
			return "调整区域: 定义的调整生效的区域.";
		break;
	case TILE_OLDLASER:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "全局旧激光: 启用旧激光模式. 激光不能击中自己, 霰弹枪会将其他目标始终拉向玩家, 即使目标已经弹开也不例外. 仅可在地图上的任意位置放置一块图块.";
		break;
	case TILE_NPC:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "全局关闭碰撞: 没有人可以与其他人碰撞.只在地图某处放置一个图块.";
		break;
	case TILE_EHOOK:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "全局开启无限钩: 所有人都有无限钩子.只在地图某处放置一个图块.";
		break;
	case TILE_NOHIT:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "全局关闭武器命中其他人: 没有人可以用武器攻击其他人.只在地图某处放置一个图块.";
		break;
	case TILE_NPH:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "全局关闭钩其他人: 没有人可以钩住其他人.只在地图某处放置一个图块.";
		break;
	case TILE_UNLOCK_TEAM:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "解锁团队: 强制解锁团队,使得一人死亡时团队不会被杀死.";
		break;
	case TILE_ADD_TIME:
		if(Layer == LAYER_SWITCH)
			return "惩罚: 在你当前的竞赛时间上增加时间.与奖励相反.";
		break;
	case TILE_NPC_DISABLE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "关闭碰撞: 你不能与其他人碰撞.";
		break;
	case TILE_UNLIMITED_JUMPS_DISABLE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "关闭无限跳: 你没有无限跳.";
		break;
	case TILE_JETPACK_DISABLE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "关闭喷气背包: 你失去了喷气背包.";
		break;
	case TILE_NPH_DISABLE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "关闭钩其他人: 你不能钩住其他人.";
		break;
	case TILE_SUBTRACT_TIME:
		if(Layer == LAYER_SWITCH)
			return "奖励: 从你当前的竞赛时间中减少时间.与惩罚相反.";
		break;
	case TILE_NPC_ENABLE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "碰撞: 你可以与其他人碰撞.";
		break;
	case TILE_UNLIMITED_JUMPS_ENABLE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "无限跳: 你有无限跳!";
		break;
	case TILE_JETPACK_ENABLE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "喷气背包: 你有一把喷气背包枪.";
		break;
	case TILE_NPH_ENABLE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "可钩其他人: 你可以钩住其他人.";
		break;
	case TILE_CREDITS_1:
	case TILE_CREDITS_2:
	case TILE_CREDITS_3:
	case TILE_CREDITS_4:
	case TILE_CREDITS_5:
	case TILE_CREDITS_6:
	case TILE_CREDITS_7:
	case TILE_CREDITS_8:
		return "制作人员: 谁设计了这些实体.";
	case TILE_ENTITIES_OFF_1:
	case TILE_ENTITIES_OFF_2:
		return "实体关闭标记: 通知使用实体玩游戏的人关于地图上的重要标记、提示、信息或文本.";
	// Entities
	case ENTITY_OFFSET + ENTITY_SPAWN:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "出生点: tee在加入游戏或在地图某处死亡后会出现在这里.";
		break;
	case ENTITY_OFFSET + ENTITY_SPAWN_RED:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "出生点: 红队成员在这里出生,与DDRace中的普通出生点相同.";
		break;
	case ENTITY_OFFSET + ENTITY_SPAWN_BLUE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "出生点: 蓝队成员在这里出生,与DDRace中的普通出生点相同.";
		break;
	case ENTITY_OFFSET + ENTITY_FLAGSTAND_RED:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "旗帜: 在DDRace中未使用.放置红队旗帜的地方.";
		break;
	case ENTITY_OFFSET + ENTITY_FLAGSTAND_BLUE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "旗帜: 在DDRace中未使用.放置蓝队旗帜的地方.";
		break;
	case ENTITY_OFFSET + ENTITY_ARMOR_1:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "盾牌: 拿走所有武器(锤子和手枪除外).";
		break;
	case ENTITY_OFFSET + ENTITY_HEALTH_1:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "冻结心: 像冻结图块一样工作.默认冻结tee 3秒.";
		break;
	case ENTITY_OFFSET + ENTITY_WEAPON_SHOTGUN:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "霰弹枪: 将tee拖向它.会从墙壁弹开.";
		break;
	case ENTITY_OFFSET + ENTITY_WEAPON_GRENADE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "榴弹发射器: 发射可推进tee的榴弹.也称为火箭.";
		break;
	case ENTITY_OFFSET + ENTITY_POWERUP_NINJA:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "忍者: 让你在最黑暗的夜晚中隐形XD";
		break;
	case ENTITY_OFFSET + ENTITY_WEAPON_LASER:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "激光: 解冻被击中的tee,会从墙壁弹开.";
		break;
	case ENTITY_OFFSET + ENTITY_LASER_FAST_CCW:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "旋转激光: 冻结激光(用激光长度制作)开始的图块.逆时针,快速.";
		break;
	case ENTITY_OFFSET + ENTITY_LASER_NORMAL_CCW:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "旋转激光: 冻结激光(用激光长度制作)开始的图块.逆时针,中速.";
		break;
	case ENTITY_OFFSET + ENTITY_LASER_SLOW_CCW:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "旋转激光: 冻结激光(用激光长度制作)开始的图块.逆时针,慢速.";
		break;
	case ENTITY_OFFSET + ENTITY_LASER_STOP:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "非旋转激光: 冻结激光(用激光长度制作)开始的图块.";
		break;
	case ENTITY_OFFSET + ENTITY_LASER_SLOW_CW:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "旋转激光: 冻结激光(用激光长度制作)开始的图块.顺时针,慢速.";
		break;
	case ENTITY_OFFSET + ENTITY_LASER_NORMAL_CW:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "旋转激光: 冻结激光(用激光长度制作)开始的图块.顺时针,中速.";
		break;
	case ENTITY_OFFSET + ENTITY_LASER_FAST_CW:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "旋转激光: 冻结激光(用激光长度制作)开始的图块.顺时针,快速.";
		break;
	case ENTITY_OFFSET + ENTITY_LASER_SHORT:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "激光长度: 放在门或旋转激光旁边,使其長3个图块长.";
		break;
	case ENTITY_OFFSET + ENTITY_LASER_MEDIUM:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "激光长度: 放在门或旋转激光旁边,使其長6个图块长.";
		break;
	case ENTITY_OFFSET + ENTITY_LASER_LONG:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "激光长度: 放在门或旋转激光旁边,使其長9个图块长.";
		break;
	case ENTITY_OFFSET + ENTITY_LASER_C_SLOW:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "激光长度变化: 放在激光长度旁边,使其不断变长和缩短.仅适用于(非)旋转激光,不适用于门.变长,慢速.";
		break;
	case ENTITY_OFFSET + ENTITY_LASER_C_NORMAL:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "激光长度变化: 放在激光长度旁边,使其不断变长和缩短.仅适用于(非)旋转激光,不适用于门.变长,中速.";
		break;
	case ENTITY_OFFSET + ENTITY_LASER_C_FAST:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "激光长度变化: 放在激光长度旁边,使其不断变长和缩短.仅适用于(非)旋转激光,不适用于门.变长,快速.";
		break;
	case ENTITY_OFFSET + ENTITY_LASER_O_SLOW:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "激光长度变化: 放在激光长度旁边,使其不断变长和缩短.仅适用于(非)旋转激光,不适用于门.缩短,慢速.";
		break;
	case ENTITY_OFFSET + ENTITY_LASER_O_NORMAL:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "激光长度变化: 放在激光长度旁边,使其不断变长和缩短.仅适用于(非)旋转激光,不适用于门.缩短,中速.";
		break;
	case ENTITY_OFFSET + ENTITY_LASER_O_FAST:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "激光长度变化: 放在激光长度旁边,使其不断变长和缩短.仅适用于(非)旋转激光,不适用于门.缩短,快速.";
		break;
	case ENTITY_OFFSET + ENTITY_PLASMAE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "炮塔: 向最近的tee射击等离子子弹.它们在击中障碍物(墙或tee)时会爆炸.";
		break;
	case ENTITY_OFFSET + ENTITY_PLASMAF:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "炮塔: 向最近的tee射击像冻结一样工作的等离子子弹.";
		break;
	case ENTITY_OFFSET + ENTITY_PLASMA:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "炮塔: 向最近的tee射击像冻结一样工作的等离子子弹.它们还会在击中障碍物(墙或tee)时爆炸.";
		break;
	case ENTITY_OFFSET + ENTITY_PLASMAU:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "炮塔: 向最近的tee射击像解冻一样工作的等离子子弹.";
		break;
	case ENTITY_OFFSET + ENTITY_CRAZY_SHOTGUN_EX:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "爆炸子弹: 从墙壁弹开时会爆炸.接触子弹像冻结图块一样工作(默认冻结3秒).";
		break;
	case ENTITY_OFFSET + ENTITY_CRAZY_SHOTGUN:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "子弹: 从墙壁弹开不会爆炸.接触子弹像冻结图块一样工作(默认冻结3秒).";
		break;
	case ENTITY_OFFSET + ENTITY_ARMOR_SHOTGUN:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "霰弹枪盾牌: 拿走霰弹枪.";
		break;
	case ENTITY_OFFSET + ENTITY_ARMOR_GRENADE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "榴弹盾牌: 拿走榴弹.";
		break;
	case ENTITY_OFFSET + ENTITY_ARMOR_NINJA:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "忍者盾牌: 拿走忍者.";
		break;
	case ENTITY_OFFSET + ENTITY_ARMOR_LASER:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "激光盾牌: 拿走激光.";
		break;
	case ENTITY_OFFSET + ENTITY_DRAGGER_WEAK:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "拖拽激光: 抓取并吸引最近的tee.无法穿过墙壁和激光阻挡器到达tee.弱.";
		break;
	case ENTITY_OFFSET + ENTITY_DRAGGER_NORMAL:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "拖拽激光: 抓取并吸引最近的tee.无法穿过墙壁和激光阻挡器到达tee.中等强度.";
		break;
	case ENTITY_OFFSET + ENTITY_DRAGGER_STRONG:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "拖拽激光: 抓取并吸引最近的tee.无法穿过墙壁和激光阻挡器到达tee.强.";
		break;
	case ENTITY_OFFSET + ENTITY_DRAGGER_WEAK_NW:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "拖拽激光: 抓取并吸引最近的tee.可以穿过墙壁但不能穿过激光阻挡器到达tee.弱.";
		break;
	case ENTITY_OFFSET + ENTITY_DRAGGER_NORMAL_NW:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "拖拽激光: 抓取并吸引最近的tee.可以穿过墙壁但不能穿过激光阻挡器到达tee.中等强度.";
		break;
	case ENTITY_OFFSET + ENTITY_DRAGGER_STRONG_NW:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "拖拽激光: 抓取并吸引最近的tee.可以穿过墙壁但不能穿过激光阻挡器到达tee.强.";
		break;
	case ENTITY_OFFSET + ENTITY_DOOR:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT || Layer == LAYER_SWITCH)
			return "门: 与激光长度结合创建门.不允许通过(只有忍者可以).";
		break;
	case TILE_TELE_GUN_ENABLE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "传送枪: 将手枪开启为传送枪武器.";
		break;
	case TILE_TELE_GUN_DISABLE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "传送枪关闭: 将手枪关闭作为传送枪武器.";
		break;
	case TILE_ALLOW_TELE_GUN:
		if(Layer == LAYER_FRONT)
			return "传送枪: 放在碰撞图块上方,激活一个传送点,取消移动.";
		if(Layer == LAYER_SWITCH)
			return "传送枪: 放在碰撞图块上方,激活一个传送点,取消移动,适用于单个武器,使用延迟编号选择哪个.";
		break;
	case TILE_ALLOW_BLUE_TELE_GUN:
		if(Layer == LAYER_FRONT)
			return "传送枪: 放在碰撞图块上方,激活一个传送点,保留移动.";
		if(Layer == LAYER_SWITCH)
			return "传送枪: 放在碰撞图块上方,激活一个传送点,保留移动,适用于单个武器,使用延迟编号选择哪个.";
		break;
	case TILE_TELE_GRENADE_ENABLE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "传送榴弹: 将榴弹开启为传送枪武器.";
		break;
	case TILE_TELE_GRENADE_DISABLE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "传送榴弹关闭: 将榴弹关闭作为传送枪武器.";
		break;
	case TILE_TELE_LASER_ENABLE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "传送激光: 将激光开启为传送枪武器.";
		break;
	case TILE_TELE_LASER_DISABLE:
		if(Layer == LAYER_GAME || Layer == LAYER_FRONT)
			return "传送激光关闭: 将激光关闭作为传送枪武器.";
		break;
	}
	if(Tile >= TILE_TIME_CHECKPOINT_FIRST && Tile <= TILE_TIME_CHECKPOINT_LAST && (Layer == LAYER_GAME || Layer == LAYER_FRONT))
		return "时间检查点: 将你当前的跑图时间与你的记录进行比较,显示你跑得更快还是更慢.";
	return nullptr;
}

const char *CExplanations::ExplainFNG(int Tile, int Layer)
{
	switch(Tile)
	{
	case TILE_PUB_AIR:
		return "空白: 可用作橡皮擦.";
	case TILE_PUB_HOOKABLE:
		if(Layer == LAYER_GAME)
			return "可钩: 可以钩住并与其碰撞.";
		break;
	case TILE_PUB_DEATH:
		if(Layer == LAYER_GAME)
			return "死亡: 杀死tee.";
		break;
	case TILE_PUB_UNHOOKABLE:
		if(Layer == LAYER_GAME)
			return "不可钩: 无法钩住,但可以与其碰撞.";
		break;
	case TILE_FNG_SPIKE_GOLD:
		if(Layer == LAYER_GAME)
			return "金色尖刺: 杀死tee并给予杀手分数.给予的分数在服务器中设置.";
		break;
	case TILE_FNG_SPIKE_NORMAL:
		if(Layer == LAYER_GAME)
			return "普通尖刺: 杀死tee并给予杀手分数.给予的分数在服务器中设置.";
		break;
	case TILE_FNG_SPIKE_RED:
		if(Layer == LAYER_GAME)
			return "红色尖刺: 红队尖刺.当杀手在蓝队时给予负分.给予的分数在服务器中设置.";
		break;
	case TILE_FNG_SPIKE_BLUE:
		if(Layer == LAYER_GAME)
			return "蓝色尖刺: 蓝队尖刺.当杀手在红队时给予负分.给予的分数在服务器中设置.";
		break;
	case TILE_FNG_SCORE_RED:
		if(Layer == LAYER_GAME)
			return "分数: 用于使用激光文本显示红队分数的旧图块.在FNG2中不再可用.";
		break;
	case TILE_FNG_SCORE_BLUE:
		if(Layer == LAYER_GAME)
			return "分数: 用于使用激光文本显示蓝队分数的旧图块.在FNG2中不再可用.";
		break;
	case TILE_FNG_SPIKE_GREEN:
		if(Layer == LAYER_GAME)
			return "绿色尖刺: 杀死tee并给予杀手分数.给予的分数在服务器中设置.";
		break;
	case TILE_FNG_SPIKE_PURPLE:
		if(Layer == LAYER_GAME)
			return "紫色尖刺: 杀死tee并给予杀手分数.给予的分数在服务器中设置.";
		break;
	case TILE_FNG_SPAWN:
		if(Layer == LAYER_GAME)
			return "出生点: tee在加入游戏或死亡后会出现在这里.";
		break;
	case TILE_FNG_SPAWN_RED:
		if(Layer == LAYER_GAME)
			return "出生点: 红队成员在这里出生.";
		break;
	case TILE_FNG_SPAWN_BLUE:
		if(Layer == LAYER_GAME)
			return "出生点: 蓝队成员在这里出生.";
		break;
	case TILE_FNG_FLAG_RED:
		if(Layer == LAYER_GAME)
			return "旗帜: 在FNG中未使用.放置红队旗帜的地方.";
		break;
	case TILE_FNG_FLAG_BLUE:
		if(Layer == LAYER_GAME)
			return "旗帜: 在FNG中未使用.放置蓝队旗帜的地方.";
		break;
	case TILE_FNG_SHIELD:
		if(Layer == LAYER_GAME)
			return "盾牌: 在FNG中无作用.";
		break;
	case TILE_FNG_HEART:
		if(Layer == LAYER_GAME)
			return "心: 在FNG中无作用.";
		break;
	case TILE_FNG_SHOTGUN:
		if(Layer == LAYER_GAME)
			return "霰弹枪: 在FNG中未使用.给你带有10发子弹的霰弹枪.";
		break;
	case TILE_FNG_GRENADE:
		if(Layer == LAYER_GAME)
			return "榴弹: 给你带有10发子弹的榴弹武器.在FNG中不太有用.";
		break;
	case TILE_FNG_NINJA:
		if(Layer == LAYER_GAME)
			return "忍者: 在FNG中无作用.";
		break;
	case TILE_FNG_LASER:
		if(Layer == LAYER_GAME)
			return "激光: 给你带有10发子弹的激光武器.在FNG中不太有用.";
		break;
	case TILE_FNG_SPIKE_OLD1:
	case TILE_FNG_SPIKE_OLD2:
	case TILE_FNG_SPIKE_OLD3:
		if(Layer == LAYER_GAME)
			return "尖刺: 旧FNG尖刺.已废弃.";
		break;
	}
	if((Tile >= TILE_PUB_CREDITS1 && Tile <= TILE_PUB_CREDITS8) && Layer == LAYER_GAME)
		return "制作人员: 谁设计了这些实体.";
	else if((Tile == TILE_PUB_ENTITIES_OFF1 || Tile == TILE_PUB_ENTITIES_OFF2) && Layer == LAYER_GAME)
		return "实体关闭标记: 通知使用实体玩游戏的人关于地图上的重要标记、提示、信息或文本.";
	return nullptr;
}

const char *CExplanations::ExplainVanilla(int Tile, int Layer)
{
	switch(Tile)
	{
	case TILE_PUB_AIR:
		return "空白: 可用作橡皮擦.";
	case TILE_PUB_HOOKABLE:
		if(Layer == LAYER_GAME)
			return "可钩: 可以钩住并与其碰撞.";
		break;
	case TILE_PUB_DEATH:
		if(Layer == LAYER_GAME)
			return "死亡: 杀死tee.";
		break;
	case TILE_PUB_UNHOOKABLE:
		if(Layer == LAYER_GAME)
			return "不可钩: 无法钩住,但可以与其碰撞.";
		break;
	case TILE_VANILLA_SPAWN:
		if(Layer == LAYER_GAME)
			return "出生点: tee在加入游戏或死亡后会出现在这里.";
		break;
	case TILE_VANILLA_SPAWN_RED:
		if(Layer == LAYER_GAME)
			return "出生点: 红队成员在这里出生.";
		break;
	case TILE_VANILLA_SPAWN_BLUE:
		if(Layer == LAYER_GAME)
			return "出生点: 蓝队成员在这里出生.";
		break;
	case TILE_VANILLA_FLAG_RED:
		if(Layer == LAYER_GAME)
			return "旗帜: 放置红队旗帜的地方.";
		break;
	case TILE_VANILLA_FLAG_BLUE:
		if(Layer == LAYER_GAME)
			return "旗帜: 放置蓝队旗帜的地方.";
		break;
	case TILE_VANILLA_SHIELD:
		if(Layer == LAYER_GAME)
			return "盾牌: 给玩家+1盾牌.";
		break;
	case TILE_VANILLA_HEART:
		if(Layer == LAYER_GAME)
			return "心: 给玩家+1生命";
		break;
	case TILE_VANILLA_SHOTGUN:
		if(Layer == LAYER_GAME)
			return "霰弹枪: 给你带有10发子弹的霰弹枪武器.";
		break;
	case TILE_VANILLA_GRENADE:
		if(Layer == LAYER_GAME)
			return "榴弹: 给你带有10发子弹的榴弹武器.";
		break;
	case TILE_VANILLA_NINJA:
		if(Layer == LAYER_GAME)
			return "忍者: 在一段时间内给你忍者.";
		break;
	case TILE_VANILLA_LASER:
		if(Layer == LAYER_GAME)
			return "激光: 给你带有10发子弹的激光武器.";
		break;
	}
	if((Tile >= TILE_PUB_CREDITS1 && Tile <= TILE_PUB_CREDITS8) && Layer == LAYER_GAME)
		return "制作人员: 谁设计了这些实体.";
	else if((Tile == TILE_PUB_ENTITIES_OFF1 || Tile == TILE_PUB_ENTITIES_OFF2) && Layer == LAYER_GAME)
		return "实体关闭标记: 通知使用实体玩游戏的人关于地图上的重要标记、提示、信息或文本.";
	return nullptr;
}

const char *CExplanations::Explain(EGametype Gametype, int Tile, int Layer)
{
	switch(Gametype)
	{
	case EGametype::NONE:
		return nullptr;
	case EGametype::DDNET:
		return ExplainDDNet(Tile, Layer);
	case EGametype::FNG:
		return ExplainFNG(Tile, Layer);
	case EGametype::RACE:
		return nullptr; // TODO: Explanations for Race
	case EGametype::VANILLA:
		return ExplainVanilla(Tile, Layer);
	case EGametype::BLOCKWORLDS:
		return nullptr; // TODO: Explanations for Blockworlds
	}
	dbg_assert_failed("Gametype invalid: %d", (int)Gametype);
}
