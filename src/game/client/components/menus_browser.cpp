/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/dbg.h>
#include <base/log.h>
#include <base/perf_timer.h>

#include <engine/client/serverbrowser.h>
#include <engine/engine.h>
#include <engine/favorites.h>
#include <engine/friends.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/BrowserTableLabel.h>
#include <game/client/QmUi/QmColorGradient.h>
#include <game/client/QmUi/QmDropdown.h>
#include <game/client/QmUi/QmIconSwap.h>
#include <game/client/QmUi/QmListEntryAnimation.h>
#include <game/client/QmUi/QmNeonRainbow.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiDiscreteSlider.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiMotion.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiSurfaceText.h>
#include <game/client/QmUi/UiTag.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/animstate.h>
#include <game/client/components/background.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/qmclient/browser_column_layout.h>
#include <game/client/components/qmclient/browser_map_preview.h>
#include <game/client/components/qmclient/browser_map_preview_loader.h>
#include <game/client/components/qmclient/browser_status_layout.h>
#include <game/client/components/qmclient/dummy_mini_view_layout.h>
#include <game/client/components/qmclient/favorite_map_vote.h>
#include <game/client/components/qmclient/friends_category_drag.h>
#include <game/client/components/qmclient/local_save_display.h>
#include <game/client/components/qmclient/map_history_ui.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/qm_icon_label.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/layers.h>
#include <game/localization.h>
#include <game/voting.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>

namespace
{
	// 收藏切换时登记一次地址，供服务器列表的星标播一次进场动画。行渲染消费后立即移除，
	// 因此表大小只与「正在进场的星标数」相关，不随浏览过的服务器数量增长。
	std::set<std::string> gs_FavoriteStarGrowPending;
}
#include <vector>

using namespace FontIcons;

namespace
{
	// 切换服务器的二次确认：Connect() 只负责弹一次，结果由服务器浏览器页每帧轮询。
	// 状态放在文件级，是因为弹窗来自 Connect()，轮询点在 RenderServerbrowser()。
	CUi::SConfirmPopupContext s_SwitchServerConfirm;
	bool s_SwitchServerPending = false;

	// 删除好友的二次确认：触发点只记一次，结果同样由浏览器页轮询。
	CUi::SConfirmPopupContext s_RemoveFriendConfirm;
	bool s_RemoveFriendPending = false;
} // namespace

static constexpr ColorRGBA gs_HighlightedTextColor = ColorRGBA(0.4f, 0.4f, 1.0f, 1.0f);
static constexpr ColorRGBA gs_QmClientCountColor = ColorRGBA(0.75f, 0.55f, 1.0f, 1.0f);
static constexpr float SERVER_LIST_TEXT_SIZE = 11.0f;
static constexpr float SERVER_LIST_SCROLLBAR_RAIL_ALPHA_SCALE = 0.28f;
// 筛选工具箱里的三张胶囊列表（社区 / 国家 / 类型）的行高与可见行数。
// 槽位高度、列表渲染和滚动步进都从这里取，卡片高度不会和实际行数差半行。
static constexpr float COMMUNITY_FILTER_ROW_HEIGHT = 13.0f;
static constexpr float COUNTRY_FILTER_ROW_HEIGHT = 18.0f;
static constexpr float TYPE_FILTER_ROW_HEIGHT = 13.0f;
static constexpr int COMMUNITY_FILTER_VISIBLE_ROWS = 4;
static constexpr int COUNTRY_FILTER_VISIBLE_ROWS = 4;

// 筛选胶囊列表的整数行占位：N 行行高 + 行间 (N-1) 档 + 上下两档容器内边距，
// 与列表里「内边距 → 行高 → 行距 → … → 行高 → 内边距」的步进严格相等，
// 即 N × (行高 + 行距) + 2 × ContainerPadding(行高)。
static inline float FilterGridExtent(int Rows, float RowHeight)
{
	return Rows * (RowHeight + ui_token::list::ROW_SPACING) + 2.0f * ui_token::list::ContainerPadding(RowHeight);
}

static SQmDropdownVisualStyle FriendsPopupVisualStyle()
{
	return QmSettingsDropdownVisualStyle(ResolveInputFallbackTheme(), color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiCardBorderColor, true)));
}

static SPopupMenuProperties FriendsPopupProperties()
{
	const SQmDropdownVisualStyle Style = FriendsPopupVisualStyle();
	SPopupMenuProperties Props;
	Props.m_BackgroundColor = Style.m_PopupBackgroundColor;
	Props.m_BorderColor = Style.m_PopupBorderColor;
	return Props;
}

static void StyleFriendsSelectionPopup(CUi::SSelectionPopupContext &Context)
{
	const SQmDropdownVisualStyle Style = FriendsPopupVisualStyle();
	Context.m_Props.m_BackgroundColor = Style.m_PopupBackgroundColor;
	Context.m_Props.m_BorderColor = Style.m_PopupBorderColor;
	Context.m_ActiveEntryColor = Style.m_ActiveEntryColor;
	Context.m_HoverEntryColor = Style.m_ActiveEntryColor;
	Context.m_TransparentButtons = Style.m_TransparentEntries;
	Context.m_EntryHeight = 18.0f;
	Context.m_EntryPadding = 3.0f;
	Context.m_EntrySpacing = 3.0f;
	Context.m_FontSize = ui_token::font::SMALL + 1.0f;
	Context.m_MinimumFontSize = ui_token::font::SMALL;
	Context.m_Width = 220.0f;
}

// 浏览器内灰度描边/覆盖色的统一透明通道：灰度色按「界面表面」（qm_ui_color）
// 染色，alpha 跟随「界面背景」透明度（qm_ui_opacity）。
static ColorRGBA BrowserOpacityColor(ColorRGBA Color, float AlphaScale = 1.0f)
{
	if(Color.r == Color.g && Color.g == Color.b)
	{
		const ColorRGBA Base = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiColor));
		Color.r = std::clamp(Base.r * (0.35f + Color.r * 0.65f), 0.0f, 1.0f);
		Color.g = std::clamp(Base.g * (0.35f + Color.g * 0.65f), 0.0f, 1.0f);
		Color.b = std::clamp(Base.b * (0.35f + Color.b * 0.65f), 0.0f, 1.0f);
	}
	Color.a = std::clamp(Color.a * (g_Config.m_QmUiOpacity / 100.0f) * AlphaScale, 0.0f, 1.0f);
	return Color;
}

class CUiBackgroundAlphaScaleScope
{
	CUi *m_pUi;
	float m_SavedAlphaScale;

public:
	CUiBackgroundAlphaScaleScope(CUi *pUi, float AlphaScale) :
		m_pUi(pUi),
		m_SavedAlphaScale(pUi->BackgroundAlphaScale())
	{
		m_pUi->SetBackgroundAlphaScale(AlphaScale);
	}

	~CUiBackgroundAlphaScaleScope()
	{
		m_pUi->SetBackgroundAlphaScale(m_SavedAlphaScale);
	}
};

static const char *FavoriteMapCategoryKeyFromText(const char *pText)
{
	if(!pText || pText[0] == '\0')
		return nullptr;
	if(str_find_nocase(pText, "DDmaX") || str_find(pText, "古典"))
	{
		if(str_find_nocase(pText, "Easy"))
			return "DDmaX Easy";
		if(str_find_nocase(pText, "Next"))
			return "DDmaX Next";
		if(str_find_nocase(pText, "Pro"))
			return "DDmaX Pro";
		if(str_find_nocase(pText, "Nut"))
			return "DDmaX Nut";
		return "DDmaX";
	}
	if(str_find_nocase(pText, "Oldschool") || str_find(pText, "传统"))
		return "Oldschool";
	if(str_find_nocase(pText, "Novice") || str_find(pText, "普通") || str_find(pText, "简单"))
		return "Novice";
	if(str_find_nocase(pText, "Moderate") || str_find(pText, "中阶"))
		return "Moderate";
	if(str_find_nocase(pText, "Brutal") || str_find(pText, "高阶") || str_find(pText, "困难"))
		return "Brutal";
	if(str_find_nocase(pText, "Insane") || str_find(pText, "疯狂"))
		return "Insane";
	if(str_find_nocase(pText, "Dummy") || str_find(pText, "分身"))
		return "Dummy";
	if(str_find_nocase(pText, "Solo") || str_find(pText, "单人"))
		return "Solo";
	if(str_find_nocase(pText, "Race"))
		return "Race";
	if(str_find_nocase(pText, "Event") || str_find(pText, "活动"))
		return "Event";
	if(str_find_nocase(pText, "Fun") || str_find(pText, "娱乐"))
		return "Fun";
	return nullptr;
}

static const char *FavoriteMapCategoryDisplayName(const char *pType)
{
	if(!pType || pType[0] == '\0')
		return Localize("Unknown");
	if(str_comp_nocase(pType, "DDmaX Easy") == 0)
		return Localize("DDmaX Easy");
	if(str_comp_nocase(pType, "DDmaX Next") == 0)
		return Localize("DDmaX Next");
	if(str_comp_nocase(pType, "DDmaX Pro") == 0)
		return Localize("DDmaX Pro");
	if(str_comp_nocase(pType, "DDmaX Nut") == 0)
		return Localize("DDmaX Nut");
	if(str_comp_nocase(pType, "DDmaX") == 0)
		return Localize("DDmaX");
	if(str_comp_nocase(pType, "Novice") == 0)
		return Localize("Novice");
	if(str_comp_nocase(pType, "Moderate") == 0)
		return Localize("Moderate");
	if(str_comp_nocase(pType, "Brutal") == 0)
		return Localize("Brutal");
	if(str_comp_nocase(pType, "Insane") == 0)
		return Localize("Insane");
	if(str_comp_nocase(pType, "Dummy") == 0)
		return Localize("Dummy");
	if(str_comp_nocase(pType, "Solo") == 0)
		return Localize("Solo");
	if(str_comp_nocase(pType, "Oldschool") == 0)
		return Localize("Oldschool");
	if(str_comp_nocase(pType, "Race") == 0)
		return Localize("Race");
	if(str_comp_nocase(pType, "Fun") == 0)
		return Localize("Fun");
	if(str_comp_nocase(pType, "Event") == 0)
		return Localize("Event");
	return Localize("Unknown");
}

// Axiom 公告的类别是玩家可见的原文，直接显示，避免「普通」被归并成「简单」。
static const char *MapDifficultyCategoryText(const CServerInfo &Server, const CQmMapDifficultyCatalog::SEntry &Entry)
{
	if(str_comp_nocase(Server.m_aCommunityId, "axiom") == 0)
		return Entry.m_Category.c_str();
	const char *pKey = FavoriteMapCategoryKeyFromText(Entry.m_Category.c_str());
	if(pKey != nullptr)
		return FavoriteMapCategoryDisplayName(pKey);
	return Entry.m_Category.c_str();
}

// 独立难度列显示「类别 + 星级」（如「简单 ★★☆☆☆」）；
// 无目录条目返回空串，由调用方显示占位符。
static void FormatMapDifficultyText(const CServerInfo &Server, const CQmMapDifficultyCatalog::SEntry *pDifficulty, char *pOut, size_t OutSize)
{
	if(pOut == nullptr || OutSize == 0)
		return;
	pOut[0] = '\0';
	if(pDifficulty == nullptr)
		return;
	pDifficulty->CellLabel(MapDifficultyCategoryText(Server, *pDifficulty), pOut, OutSize);
}

// 浏览器名称列的区段短名：解析器输出「类别 - 区段」（如「简单 - CHN5 上海」），
// 名称列剥掉「类别 - 」前缀只留区段；类别信息由难度列恒定承担。
// 好友菜单沿用完整解析短名（既有测试钉住），不受影响。
static const char *BrowserSegmentDisplayName(const char *pParsedShortName, char *pBuffer, size_t BufferSize)
{
	if(const char *pSep = str_find(pParsedShortName, " - "))
	{
		const char *pSegment = str_skip_whitespaces_const(pSep + 3);
		if(pSegment[0] != '\0')
		{
			str_copy(pBuffer, pSegment, BufferSize);
			return pBuffer;
		}
	}
	return pParsedShortName;
}

static const char *MapCategoryHintFromServer(const CServerInfo *pServer)
{
	if(pServer == nullptr)
		return nullptr;
	const char *pCategory = FavoriteMapCategoryKeyFromText(pServer->m_aName);
	return pCategory != nullptr ? pCategory : FavoriteMapCategoryKeyFromText(pServer->m_aCommunityType);
}

static bool IsClanMembersCategory(const char *pCategory)
{
	return pCategory != nullptr && str_comp_nocase(pCategory, IFriends::CLAN_MEMBERS_CATEGORY) == 0;
}

static bool IsOfflineFriendsCategory(const char *pCategory)
{
	return pCategory != nullptr && str_comp_nocase(pCategory, IFriends::OFFLINE_CATEGORY) == 0;
}

static bool IsProtectedFriendsCategory(const char *pCategory)
{
	return pCategory != nullptr && (str_comp_nocase(pCategory, IFriends::DEFAULT_CATEGORY) == 0 || IsClanMembersCategory(pCategory) || IsOfflineFriendsCategory(pCategory));
}

static const char *LocalizeFriendsCategory(const char *pCategory)
{
	if(pCategory == nullptr || pCategory[0] == '\0')
		return "";
	if(str_comp_nocase(pCategory, IFriends::DEFAULT_CATEGORY) == 0)
		return Localize("Friends");
	if(IsClanMembersCategory(pCategory))
		return Localize("Clan Members");
	if(IsOfflineFriendsCategory(pCategory))
		return Localize("Offline");
	return pCategory;
}

// 好友行动作图标只有 12 左右的字形，横向相邻很紧；只加高到 20，
// 避免 20 方形热区互相重叠后把点击误判成相邻动作。
static CUIRect FriendActionHitRect(const CUIRect &IconRect)
{
	CUIRect Hit = IconRect;
	const float Height = std::max(Hit.h, 20.0f);
	Hit.y -= (Height - Hit.h) * 0.5f;
	Hit.h = Height;
	return Hit;
}

// 沿圆角卡片边缘取点；距离按实际周长回绕，圆弧段用半径参数化，避免折角处颜色跳变。
struct SFriendRingSample
{
	vec2 m_Pos;
	vec2 m_Normal;
};

static SFriendRingSample FriendRingSample(const CUIRect &Rect, float Radius, float Distance)
{
	const float Width = maximum(0.0f, Rect.w);
	const float Height = maximum(0.0f, Rect.h);
	Radius = minimum(Radius, minimum(Width, Height) * 0.5f);
	const float StraightWidth = maximum(0.0f, Width - 2.0f * Radius);
	const float StraightHeight = maximum(0.0f, Height - 2.0f * Radius);
	const float ArcLength = pi * Radius * 0.5f;
	const float aLengths[] = {StraightWidth, ArcLength, StraightHeight, ArcLength, StraightWidth, ArcLength, StraightHeight, ArcLength};
	const float Perimeter = 2.0f * StraightWidth + 2.0f * StraightHeight + 4.0f * ArcLength;
	if(Perimeter <= 0.001f)
		return {{Rect.x, Rect.y}, {0.0f, -1.0f}};
	Distance = std::fmod(Distance, Perimeter);
	if(Distance < 0.0f)
		Distance += Perimeter;

	const float Left = Rect.x;
	const float Top = Rect.y;
	const float Right = Rect.x + Width;
	const float Bottom = Rect.y + Height;
	float Remaining = Distance;
	for(int Segment = 0; Segment < 8; ++Segment)
	{
		const float Length = aLengths[Segment];
		if(Length <= 0.0f)
			continue;
		if(Remaining > Length)
		{
			Remaining -= Length;
			continue;
		}
		const float T = Length > 0.0f ? Remaining / Length : 0.0f;
		switch(Segment)
		{
		case 0:
			return {{Left + Radius + T * StraightWidth, Top}, {0.0f, -1.0f}};
		case 1:
		{
			const float Angle = -pi * 0.5f + T * pi * 0.5f;
			return {{Right - Radius + std::cos(Angle) * Radius, Top + Radius + std::sin(Angle) * Radius}, {std::cos(Angle), std::sin(Angle)}};
		}
		case 2:
			return {{Right, Top + Radius + T * StraightHeight}, {1.0f, 0.0f}};
		case 3:
		{
			const float Angle = T * pi * 0.5f;
			return {{Right - Radius + std::cos(Angle) * Radius, Bottom - Radius + std::sin(Angle) * Radius}, {std::cos(Angle), std::sin(Angle)}};
		}
		case 4:
			return {{Right - Radius - T * StraightWidth, Bottom}, {0.0f, 1.0f}};
		case 5:
		{
			const float Angle = pi * 0.5f + T * pi * 0.5f;
			return {{Left + Radius + std::cos(Angle) * Radius, Bottom - Radius + std::sin(Angle) * Radius}, {std::cos(Angle), std::sin(Angle)}};
		}
		case 6:
			return {{Left, Bottom - Radius - T * StraightHeight}, {-1.0f, 0.0f}};
		default:
		{
			const float Angle = pi + T * pi * 0.5f;
			return {{Left + Radius + std::cos(Angle) * Radius, Top + Radius + std::sin(Angle) * Radius}, {std::cos(Angle), std::sin(Angle)}};
		}
		}
	}
	return {{Left, Top}, {0.0f, -1.0f}};
}

static ColorRGBA FriendRainbowColor(float Hue, float Wave, float Alpha)
{
	// 与无限钩标记共用 QmNeonRainbowColor：同一界面里的两处彩虹必须是同一套公式，
	// 否则色相、饱和度和高光会各自漂移成两种不同的"彩虹"。
	return QmNeonRainbowColor(Hue, Wave, Alpha);
}

// 把彩虹色环铺成七段 ANGULAR 渐变：数量固定为 7（SDF 上限）。
// 末段与首段色相衔接，绕一圈闭合，避免接缝。
static SQmColorGradient FriendRingGradient(float Flow)
{
	SQmColorGradient Gradient;
	Gradient.m_Type = EQmGradientType::ANGULAR;
	Gradient.m_Center = vec2(0.5f, 0.5f);
	Gradient.m_Direction = vec2(1.0f, 0.0f);
	Gradient.m_Range = 1.0f;
	constexpr int NUM = (int)IGraphics::ROUNDED_RECT_SDF_GRADIENT_MAX_COLORS;
	Gradient.m_NumColors = NUM;
	for(int i = 0; i < NUM; ++i)
	{
		// 逆时针绕行；色相整体偏移产生流动感。
		const float Position = 1.0f - static_cast<float>(i) / (NUM - 1);
		Gradient.m_aColors[i] = FriendRainbowColor(0.60f + Position + Flow, 0.5f, 1.0f);
	}
	return Gradient;
}

// 跟随中的好友用一条沿卡片的彩虹流光描边表示持续连接：颜色绕卡片渐变并缓慢流动，
// 高光带和呼吸光晕独立循环；停止跟随后随标签一起淡出。
static void DrawFriendFollowRing(CUi *pUi, const CUIRect &Rect, float Alpha, bool Animated)
{
	if(pUi == nullptr || Alpha <= 0.01f)
		return;
	// 几何必须与卡片本体完全一致：卡片的圆角是 ui_token::radius::SMALL_MENU，
	// 光环若另取半径或额外内缩，加宽卡片后两者会明显对不上。
	const float Radius = minimum(ui_token::radius::SMALL_MENU, minimum(Rect.w, Rect.h) * 0.5f);
	const CUIRect Ring = Rect;
	const float Perimeter = 2.0f * maximum(0.0f, Ring.w + Ring.h - 4.0f * Radius) + 2.0f * pi * Radius;
	if(Perimeter <= 16.0f)
		return;

	const float Now = Animated ? (float)((double)time_get() / (double)time_freq()) : 0.0f;
	const float Flow = Animated ? std::fmod(Now * 0.11f, 1.0f) : 0.0f;
	const float Breath = Animated ? 0.5f - 0.5f * std::cos(Now * 2.0f * pi / 3.2f) : 0.5f;

	// 有 SDF 时整圈交给片元着色器：角度渐变逐像素求值，圆角是解析圆弧，
	// 不再受几何分段影响。必须开 m_BorderOnly，否则渐变会把卡片内部整片染色。
	if(pUi->Graphics()->HasRoundedRectSdf())
	{
		const SQmColorGradient Gradient = FriendRingGradient(Flow);
		// 单次绘制一条很宽的环带，靠带宽内的幂次衰减本身形成连续渐变。
		// 叠多个窄环带会看到彼此的分界（"一层层"），只有一条连续衰减的带才是晕。
		// 光晕用形状外扩（m_ShapeExpand）而不是「矩形放大 + 半径加宽」：连续曲率下
		// 圆角的等距外扩不是半径加宽的同一条曲线，手动放大矩形会让光环四角与卡片
		// 错位（换档位/强度时尤其明显）。外扩走距离场偏移，任何曲线档位都同心。
		const auto EmitSdfGlow = [&](float Spread, float Falloff, float RingAlpha) {
			SRoundedSurfaceParams Params;
			Params.m_Radius = Radius;
			Params.m_ShapeExpand = Spread;
			Params.m_BorderWidth = Spread;
			Params.m_BorderUsesGradient = true;
			Params.m_BorderOnly = true;
			Params.m_BorderFalloff = Falloff;
			// 边框色的 alpha 决定整体强弱，渐变只提供色相。
			const ColorRGBA RingColor(1.0f, 1.0f, 1.0f, RingAlpha);
			DrawRoundedGradientSurface(pUi->Graphics(), Ring, Gradient, 1.0f, RingColor, Params);
		};
		// 外层：宽而柔的氤氲；内层：窄而实的彩带。两者同一套几何中心，
		// 宽度连续衔接，因此叠起来看不到分层。
		const float OuterSpread = 10.0f + Breath * 2.4f;
		EmitSdfGlow(OuterSpread, 2.4f, 0.30f * (0.70f + Breath * 0.45f) * Alpha);
		EmitSdfGlow(2.6f + Breath * 0.5f, 0.9f, 0.55f * (0.80f + Breath * 0.25f) * Alpha);
		// 最内侧的清晰彩虹描边：宽度固定，不做衰减。
		SRoundedSurfaceParams CoreParams;
		CoreParams.m_Radius = Radius;
		CoreParams.m_BorderWidth = 1.4f;
		CoreParams.m_BorderUsesGradient = true;
		CoreParams.m_BorderOnly = true;
		CoreParams.m_BorderFalloff = 0.0f;
		DrawRoundedGradientSurface(pUi->Graphics(), Ring, Gradient, 1.0f, ColorRGBA(1.0f, 1.0f, 1.0f, (0.92f + Breath * 0.08f) * Alpha), CoreParams);
		return;
	}

	// 采样按段长自适应，而不是按周长均分：卡片很宽，直边占掉绝大部分周长后，
	// 四个 90° 圆角只分到两三段，法线跨 30° 以上，圆角会被画成折线。
	// 直边按 12 单位一段，圆弧按固定角步长（≤8°）细分，圆角才是平滑弧。
	const float StraightWidth = maximum(0.0f, Ring.w - 2.0f * Radius);
	const float StraightHeight = maximum(0.0f, Ring.h - 2.0f * Radius);
	constexpr float StraightStep = 12.0f;
	constexpr float ArcStepRadians = 8.0f * pi / 180.0f;
	const int StraightWidthSegments = maximum(1, (int)std::ceil(StraightWidth / StraightStep));
	const int StraightHeightSegments = maximum(1, (int)std::ceil(StraightHeight / StraightStep));
	const int CornerSegments = maximum(2, (int)std::ceil((pi * 0.5f) / ArcStepRadians));
	const int TotalSegments = 2 * StraightWidthSegments + 2 * StraightHeightSegments + 4 * CornerSegments;

	std::vector<SFriendRingSample> vSamples;
	std::vector<ColorRGBA> vColors;
	vSamples.reserve(TotalSegments + 1);
	vColors.reserve(TotalSegments + 1);
	// 从左上圆角结束处开始，顺时针走一圈；首尾点重合，保证颜色无缝闭合。
	for(int i = 0; i <= TotalSegments; ++i)
	{
		const float Position = static_cast<float>(i) / TotalSegments;
		const float Distance = Position * Perimeter;
		vSamples.push_back(FriendRingSample(Ring, Radius, Distance));
		const float Wave = Animated ? 0.5f + 0.5f * std::sin(2.0f * pi * (Position * 2.0f - Now * 0.45f)) : 0.5f;
		vColors.push_back(FriendRainbowColor(0.60f + Position + Flow, Wave, 1.0f));
	}
	const int Segments = TotalSegments;

	pUi->Graphics()->TextureClear();
	pUi->Graphics()->QuadsBegin();
	// 光晕向外扩散形成霓虹辉光；外侧超出卡片的部分由滚动区裁剪兜底，
	// 卡片本身已随列表加宽，正常宽度下裁切量很小。
	const auto EmitGlow = [&](float InnerHalfWidth, float OuterHalfWidth, float InnerAlpha, float OuterAlpha) {
		for(int i = 0; i < Segments; ++i)
		{
			const auto &From = vSamples[i];
			const auto &To = vSamples[i + 1];
			const IGraphics::CColorVertex aVertices[] = {
				{0, vColors[i].WithAlpha(Alpha * InnerAlpha)},
				{1, vColors[i + 1].WithAlpha(Alpha * InnerAlpha)},
				{2, vColors[i].WithAlpha(Alpha * OuterAlpha)},
				{3, vColors[i + 1].WithAlpha(Alpha * OuterAlpha)}};
			pUi->Graphics()->SetColorVertex(aVertices, std::size(aVertices));
			const IGraphics::CFreeformItem Quad(
				From.m_Pos + From.m_Normal * InnerHalfWidth,
				To.m_Pos + To.m_Normal * InnerHalfWidth,
				From.m_Pos + From.m_Normal * OuterHalfWidth,
				To.m_Pos + To.m_Normal * OuterHalfWidth);
			pUi->Graphics()->QuadsDrawFreeform(&Quad, 1);
		}
	};

	// 外侧三层羽化光晕逐层收窄并提亮，内侧主环保持清晰的彩带。
	EmitGlow(0.75f, 9.0f + Breath * 2.2f, 0.24f * (0.72f + Breath * 0.45f), 0.0f);
	EmitGlow(0.65f, 5.0f + Breath * 1.0f, 0.40f * (0.75f + Breath * 0.35f), 0.0f);
	EmitGlow(0.55f, 2.4f + Breath * 0.5f, 0.60f * (0.80f + Breath * 0.25f), 0.0f);
	for(int i = 0; i < Segments; ++i)
	{
		const auto &From = vSamples[i];
		const auto &To = vSamples[i + 1];
		const float CoreAlpha = 0.92f + Breath * 0.08f;
		const IGraphics::CColorVertex aVertices[] = {
			{0, vColors[i].WithAlpha(Alpha * CoreAlpha)},
			{1, vColors[i + 1].WithAlpha(Alpha * CoreAlpha)},
			{2, vColors[i].WithAlpha(Alpha * CoreAlpha)},
			{3, vColors[i + 1].WithAlpha(Alpha * CoreAlpha)}};
		pUi->Graphics()->SetColorVertex(aVertices, std::size(aVertices));
		const IGraphics::CFreeformItem Quad(
			From.m_Pos - From.m_Normal * 0.9f,
			To.m_Pos - To.m_Normal * 0.9f,
			From.m_Pos + From.m_Normal * 0.9f,
			To.m_Pos + To.m_Normal * 0.9f);
		pUi->Graphics()->QuadsDrawFreeform(&Quad, 1);
	}
	pUi->Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	pUi->Graphics()->QuadsEnd();
}

static ColorRGBA PlayerBackgroundColor(bool Friend, bool Clan, bool Afk, bool InSelectedServer, bool Inside)
{
	const ColorRGBA FriendsColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClFriendsListFriendColor));
	const ColorRGBA ClanColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClFriendsListClanColor));
	const ColorRGBA NeutralColor = ColorRGBA(0.75f, 0.75f, 0.75f);
	const ColorRGBA COLORS[] = {FriendsColor, ClanColor, NeutralColor};
	static const ColorRGBA COLORS_AFK[] = {ColorRGBA(1.0f, 1.0f, 0.5f), ColorRGBA(1.0f, 0.98f, 0.65f), ColorRGBA(0.6f, 0.6f, 0.6f)};
	int i;
	if(Friend)
		i = 0;
	else if(Clan)
		i = 1;
	else
		i = 2;
	return (Afk ? COLORS_AFK[i] : COLORS[i]).WithAlpha(0.3f + (Inside ? 0.15f : 0.0f) + (InSelectedServer ? 0.12f : 0.0f));
}

static ColorRGBA GetPingTextColor(int Latency)
{
	return color_cast<ColorRGBA>(ColorHSLA((300.0f - std::clamp(Latency, 0, 300)) / 1000.0f, 1.0f, 0.5f));
}

// QmClient: 延迟列着色。估计延迟（无实测值）按地区匹配而非估计数值：
// 服务器与本机同地区即视为低延迟显示绿色——按地区分、不看 VPN 实测
// （估计延迟受代理/路由影响，同区也可能被高估成橙色）。
static ColorRGBA GetServerListPingColor(const CServerInfo *pInfo, int OwnLocation)
{
	if(pInfo->m_LatencyIsEstimated && pInfo->m_Location >= 0 && pInfo->m_Location == OwnLocation)
		return GetPingTextColor(0);
	return GetPingTextColor(pInfo->m_Latency);
}

void CMenus::OpenMapPreviewPopup(const CServerInfo *pItem)
{
	const auto *pDifficulty = str_comp_nocase(pItem->m_aCommunityId, "axiom") == 0 ?
					  m_MapDifficultyCatalog.FindForServer(*pItem) :
					  m_MapDifficultyCatalog.Find(pItem->m_aMap);
	const char *pStars = pDifficulty != nullptr ? pDifficulty->StarLabel() : "";
	char aCaption[192];
	if(pStars[0] != '\0')
		str_format(aCaption, sizeof(aCaption), "%s · %s · %d/%d", pStars, pItem->m_aGameType, pItem->m_NumFilteredPlayers, ServerBrowser()->Max(*pItem));
	else
		str_format(aCaption, sizeof(aCaption), "%s · %d/%d", pItem->m_aGameType, pItem->m_NumFilteredPlayers, ServerBrowser()->Max(*pItem));

	CQmMapPreviewPanel::STarget Target;
	str_copy(Target.m_aMapName, pItem->m_aMap, sizeof(Target.m_aMapName));
	str_copy(Target.m_aCaption, aCaption, sizeof(Target.m_aCaption));
	m_MapPreviewPanel.Init(GameClient());
	m_MapPreviewPanel.Open(ui_widget::ControlContext(Ui()), &m_MapPreviewPopupContext, Target);
}

void CMenus::RenderServerbrowserServerList(CUIRect View, bool &WasListboxItemActivated)
{
	static CListBox s_ListBox;
	static CQmBrowserHeaderGesture s_HeaderGesture;
	static int s_ReorderColIndex = -1;
	static int s_ReorderInsertSlot = -1;
	static float s_ReorderPressX = 0.0f;
	static uint64_t s_LastHeaderFrame = 0;
	static int s_LastHeaderRestartSeq = -1;
	static bool s_WidthsChanged = false;
	const bool HeaderInteractive = Ui()->Enabled() && !Ui()->RenderOnly() && !Ui()->PointerInputBlocked();
	bool ResetColumnWidths = false;
	// 上次保存的服务器地址只用于高亮，启动后保持顶部；用户选择和键盘导航再显式定位。
	s_ListBox.SuppressInitialSelectionScroll();
	static int s_LastRestartSeq = -1;
	if(s_LastRestartSeq != m_BrowserListEntryRestartSeq)
	{
		s_ListBox.RestartEntryAnimation();
		s_LastRestartSeq = m_BrowserListEntryRestartSeq;
	}

	// 难度排序依赖客户端注入的星级目录（引擎侧没有目录数据）。首次渲染列表时
	// 注入一次，并补偿启动期已持久化的难度排序——注入前的 Sort 只能拿到 -1。
	static bool s_DifficultyStarsHookInstalled = false;
	if(!s_DifficultyStarsHookInstalled)
	{
		s_DifficultyStarsHookInstalled = true;
		auto *pServerBrowserImpl = dynamic_cast<CServerBrowser *>(ServerBrowser());
		if(pServerBrowserImpl != nullptr)
		{
			pServerBrowserImpl->m_MapDifficultyStarsFunction = [this](const CServerInfo *pInfo) {
				const CQmMapDifficultyCatalog::SEntry *pEntry = pInfo != nullptr ? m_MapDifficultyCatalog.FindForServer(*pInfo, MapCategoryHintFromServer(pInfo)) : nullptr;
				return pEntry != nullptr ? pEntry->m_Stars : -1;
			};
			if(g_Config.m_BrSort == IServerBrowser::SORT_DIFFICULTY)
				pServerBrowserImpl->RequestResort();
		}
	}

	CUIRect Headers;
	View.HSplitTop(ms_ListheaderHeight, &Headers, &View);
	// 胶囊行先开：表头列宽要按行让位收，而 RowReserveWidth 由 token 直接算，
	// 不依赖上一帧解析出的滚动条宽度；表头与行胶囊右缘因此同一条公式。
	s_ListBox.SetCapsuleRows(SERVER_LIST_TEXT_SIZE);
	const float BrowserScrollbarReserve = s_ListBox.RowReserveWidth();
	Headers.VSplitRight(BrowserScrollbarReserve, &Headers, nullptr);
	// 胶囊行统一外观：表格行的行距并进行高（滚动步进与 SkipItems 都按行高走，
	// 组件再叠一档行距会把跳过行补不回来），左右内退与行内内边距同源。
	// 表头跟着同一条内退，列标题、列内容与行胶囊才会落在同一个坐标系里。
	const float BrowserRowInsetX = ui_token::list::RowInsetX(SERVER_LIST_TEXT_SIZE);
	const float BrowserRowStep = ms_ListheaderHeight + ui_token::list::ROW_SPACING;
	Headers.VSplitLeft(BrowserRowInsetX, nullptr, &Headers);
	Headers.VSplitRight(BrowserRowInsetX, &Headers, nullptr);

	{
		CUIRect ResetBtn;
		Headers.VSplitLeft(ms_ListheaderHeight, &ResetBtn, &Headers);
		// 左侧留一点呼吸边距：按钮不贴列表左边缘（右侧不动，后面各列的对齐不变）。
		ResetBtn.VSplitLeft(4.0f, nullptr, &ResetBtn);
		ResetBtn.Margin(1.0f, &ResetBtn);
		static CButtonContainer s_ResetColsButton;
		if(Ui()->DoButton_QmIcon(&s_ResetColsButton, EQmIcon::ARROW_ROTATE_RIGHT, FONT_ICON_ARROW_ROTATE_RIGHT, 0, &ResetBtn, BUTTONFLAG_LEFT))
		{
			ResetColumnWidths = true;
			s_HeaderGesture.End();
			s_ReorderColIndex = -1;
			s_ReorderInsertSlot = -1;
			s_WidthsChanged = false;
			g_Config.m_BrColWidthName = 120;
			g_Config.m_BrColWidthGametype = 68;
			g_Config.m_BrColWidthMap = 120;
			g_Config.m_BrColWidthDifficulty = 80;
			g_Config.m_BrColWidthFriends = 14;
			g_Config.m_BrColWidthPlayers = 40;
			g_Config.m_BrColWidthQmClients = 24;
			g_Config.m_BrColWidthPing = 30;
			g_Config.m_BrColOrder[0] = '\0';
			ConfigManager()->Save();
		}
		if(Ui()->HotItem() == &s_ResetColsButton)
			GameClient()->m_Tooltips.DoToolTip(&s_ResetColsButton, &ResetBtn, Localize("Reset column widths"));
	}

	struct SColumn
	{
		int m_Id;
		int m_Sort;
		const char *m_pCaption;
		int m_Direction;
		float m_Width;
		CUIRect m_Rect;
	};

	enum
	{
		COL_FLAG_LOCK = 0,
		COL_FLAG_FAV,
		COL_COMMUNITY,
		COL_NAME,
		COL_DIFFICULTY,
		COL_GAMETYPE,
		COL_MAP,
		COL_FRIENDS,
		COL_PLAYERS,
		COL_QM_CLIENTS,
		COL_PING,
	};

	enum
	{
		UI_ELEM_LOCK_ICON = 0,
		UI_ELEM_FAVORITE_ICON,
		UI_ELEM_NAME_1,
		UI_ELEM_NAME_2,
		UI_ELEM_NAME_3,
		UI_ELEM_DIFFICULTY,
		UI_ELEM_GAMETYPE,
		UI_ELEM_MAP_1,
		UI_ELEM_MAP_2,
		UI_ELEM_MAP_3,
		UI_ELEM_FINISH_ICON,
		UI_ELEM_MAP_PREVIEW_ICON,
		UI_ELEM_PLAYERS,
		UI_ELEM_FRIEND_ICON,
		UI_ELEM_QM_CLIENTS,
		UI_ELEM_PING,
		UI_ELEM_KEY_ICON,
		NUM_UI_ELEMS,
	};

	constexpr float ClickableIconSpace = 20.0f;

	// 钉住列（锁/收藏图标）+ 内容列。内容列的**数组序 = 默认视觉序**；
	// 实际每帧按 br_col_order 重排进 s_aCols（地址稳定，供 hot item 使用）。
	// 内容列共享窗口宽度预算；拖动一列时只在其右侧重新分配空间。
	static const SColumn s_aBaseCols[] = {
		{COL_FLAG_LOCK, -1, "", 0, 14.0f, {0}},
		{COL_FLAG_FAV, IServerBrowser::SORT_FAVORITES, "", 0, ClickableIconSpace, {0}},
		{COL_COMMUNITY, -1, "", 0, 28.0f, {0}},
		{COL_NAME, IServerBrowser::SORT_NAME, Localizable("Name"), 0, 50.0f, {0}},
		{COL_MAP, IServerBrowser::SORT_MAP, Localizable("Map"), 0, 120.0f, {0}},
		{COL_DIFFICULTY, IServerBrowser::SORT_DIFFICULTY, Localizable("Difficulty"), 0, 80.0f, {0}},
		{COL_GAMETYPE, IServerBrowser::SORT_GAMETYPE, Localizable("Type"), 0, 68.0f, {0}},
		{COL_FRIENDS, IServerBrowser::SORT_NUMFRIENDS, "", 0, ClickableIconSpace, {0}},
		{COL_PLAYERS, IServerBrowser::SORT_NUMPLAYERS, Localizable("Players"), 0, 60.0f, {0}},
		{COL_QM_CLIENTS, IServerBrowser::SORT_QM_CLIENTS, "梦", 0, 24.0f, {0}},
		{COL_PING, IServerBrowser::SORT_PING, Localizable("Ping"), 0, 30.0f, {0}},
	};

	// 每帧按配置顺序重排（地址稳定的静态工作数组，热项/激活项指针不悬空）。
	static SColumn s_aCols[std::size(s_aBaseCols)];
	static bool s_ColsInitialized = false;
	if(!s_ColsInitialized)
	{
		s_ColsInitialized = true;
		for(size_t i = 0; i < std::size(s_aBaseCols); i++)
			s_aCols[i] = s_aBaseCols[i];
	}

	constexpr int NumPinnedCols = 2; // 锁 / 收藏
	constexpr int NumContentCols = (int)std::size(s_aBaseCols) - NumPinnedCols;
	// 键名与 s_aBaseCols 内容列一一对应（base[NumPinnedCols + k]）。
	static constexpr const char *s_apContentColKeys[NumContentCols] = {"community", "name", "map", "difficulty", "gametype", "friends", "players", "qm", "ping"};
	// 内容列顺序：存 base 内容位（0..NumContentCols-1），每帧从 br_col_order
	// 解析（9 项开销可忽略）——重置按钮清空配置后自然回默认序，无需脏标记。
	static int s_aContentOrder[NumContentCols];
	auto ParseContentOrder = [&]() {
		int aOrder[NumContentCols];
		int Used = 0;
		bool aSeen[NumContentCols] = {};
		const char *pCursor = g_Config.m_BrColOrder;
		while(pCursor != nullptr && pCursor[0] != '\0' && Used < NumContentCols)
		{
			const char *pEnd = str_find(pCursor, ",");
			const int Len = pEnd != nullptr ? (int)(pEnd - pCursor) : str_length(pCursor);
			if(Len > 0)
			{
				for(int k = 0; k < NumContentCols; k++)
				{
					if(!aSeen[k] && str_length(s_apContentColKeys[k]) == Len && str_comp_nocase_num(pCursor, s_apContentColKeys[k], Len) == 0)
					{
						aSeen[k] = true;
						aOrder[Used++] = k;
						break;
					}
				}
			}
			pCursor = pEnd != nullptr ? pEnd + 1 : nullptr;
		}
		// 缺失/非法键按默认序补齐（空串/旧档位 = 完整默认序）。
		for(int k = 0; k < NumContentCols && Used < NumContentCols; k++)
			if(!aSeen[k])
				aOrder[Used++] = k;
		for(int i = 0; i < NumContentCols; i++)
			s_aContentOrder[i] = aOrder[i];
	};
	ParseContentOrder();

	auto SerializeContentOrder = [&]() {
		char aOrder[128];
		aOrder[0] = '\0';
		for(int i = 0; i < NumContentCols; i++)
		{
			if(i != 0)
				str_append(aOrder, ",", sizeof(aOrder));
			str_append(aOrder, s_apContentColKeys[s_aContentOrder[i]], sizeof(aOrder));
		}
		str_copy(g_Config.m_BrColOrder, aOrder, sizeof(g_Config.m_BrColOrder));
		ConfigManager()->Save();
	};

	{
		SColumn aOrdered[std::size(s_aBaseCols)];
		for(int i = 0; i < NumPinnedCols; i++)
			aOrdered[i] = s_aBaseCols[i];
		for(int i = 0; i < NumContentCols; i++)
			aOrdered[NumPinnedCols + i] = s_aBaseCols[NumPinnedCols + s_aContentOrder[i]];
		for(size_t i = 0; i < std::size(s_aBaseCols); i++)
			s_aCols[i] = aOrdered[i];
	}

	constexpr int NumCols = std::size(s_aCols);
	static std::array<SQmBrowserColumnSizing, NumCols> s_aResizeStart;
	std::array<float, NumCols> aContentWidths{};
	static std::array<SQmBrowserColumnSizing, NumCols> s_aLayoutById;
	static std::array<int, NumCols> s_aLayoutConfigById{};
	static float s_LayoutViewportWidth = -1.0f;
	static float s_ResizePressX = 0.0f;
	static float s_ResizeViewportWidth = 0.0f;
	bool MeasureColumnContent = false;

	auto GetColWidthConfig = [](int ColId) -> int * {
		switch(ColId)
		{
		case COL_NAME: return &g_Config.m_BrColWidthName;
		case COL_GAMETYPE: return &g_Config.m_BrColWidthGametype;
		case COL_MAP: return &g_Config.m_BrColWidthMap;
		case COL_DIFFICULTY: return &g_Config.m_BrColWidthDifficulty;
		case COL_FRIENDS: return &g_Config.m_BrColWidthFriends;
		case COL_PLAYERS: return &g_Config.m_BrColWidthPlayers;
		case COL_QM_CLIENTS: return &g_Config.m_BrColWidthQmClients;
		case COL_PING: return &g_Config.m_BrColWidthPing;
		default: return nullptr;
		}
	};

	auto GetColMinWidth = [](int ColId) {
		switch(ColId)
		{
		case COL_NAME: return 60.0f;
		case COL_GAMETYPE: return 62.0f;
		case COL_MAP: return 90.0f;
		case COL_DIFFICULTY: return 44.0f;
		case COL_FRIENDS: return 12.0f;
		case COL_PLAYERS: return 34.0f;
		case COL_QM_CLIENTS: return 20.0f;
		case COL_PING: return 26.0f;
		default: return 10.0f;
		}
	};

	auto GetColMaxWidth = [](int ColId) {
		switch(ColId)
		{
		case COL_GAMETYPE: return 300.0f;
		case COL_MAP: return 800.0f;
		case COL_DIFFICULTY: return 300.0f;
		case COL_FRIENDS: return 120.0f;
		case COL_PLAYERS: return 240.0f;
		case COL_QM_CLIENTS: return 120.0f;
		case COL_PING: return 180.0f;
		default: return 1000.0f;
		}
	};

	// 输入归属或页面失效后取消旧按压；只释放自己的控件，不清掉弹窗的 active item。
	const int ResizeColumn = s_HeaderGesture.ResizeColumn();
	const void *pResizeId = ResizeColumn >= 0 ? &s_aCols[ResizeColumn].m_Width : nullptr;
	if(!HeaderInteractive || s_LastHeaderRestartSeq != m_BrowserListEntryRestartSeq ||
		Client()->PerfFrame() > s_LastHeaderFrame + 1 ||
		(ResizeColumn >= 0 && (!Ui()->IsActiveItem(pResizeId) || s_ResizeViewportWidth != Headers.w)))
	{
		if(pResizeId != nullptr && Ui()->IsActiveItem(pResizeId))
			Ui()->SetActiveItem(nullptr);
		s_HeaderGesture.End();
		s_ReorderColIndex = -1;
		s_ReorderInsertSlot = -1;
	}
	s_LastHeaderFrame = Client()->PerfFrame();
	s_LastHeaderRestartSeq = m_BrowserListEntryRestartSeq;

	// 可用宽度包含所有列与间隙；窗口过窄时仅裁剪文字，字号保持不变。
	Headers.w = std::max(0.0f, Headers.w);
	const float ColGap = std::min(2.0f, Headers.w / (NumCols - 1));
	const float ColumnBudget = std::max(0.0f, Headers.w - ColGap * (NumCols - 1));
	std::array<SQmBrowserColumnSizing, NumCols> aSizing;
	bool ConfigChanged = false;
	for(int i = 0; i < NumCols; ++i)
	{
		const SColumn &Col = s_aCols[i];
		const int *pConfig = GetColWidthConfig(Col.m_Id);
		const float MinWidth = pConfig != nullptr ? GetColMinWidth(Col.m_Id) : Col.m_Width;
		const float MaxWidth = pConfig != nullptr ? GetColMaxWidth(Col.m_Id) : Col.m_Width;
		const float Width = pConfig != nullptr ? std::clamp((float)*pConfig, MinWidth, MaxWidth) : Col.m_Width;
		aSizing[i] = {Width, MinWidth, MaxWidth, Width};
		ConfigChanged |= pConfig != nullptr && s_aLayoutConfigById[Col.m_Id] != *pConfig;
	}
	// 配置命令覆盖用户旧拖动，不能再用按下快照把新值写回去。
	if(ConfigChanged && s_HeaderGesture.ResizeColumn() >= 0)
	{
		if(Ui()->IsActiveItem(pResizeId))
			Ui()->SetActiveItem(nullptr);
		s_HeaderGesture.End();
	}
	if(ResetColumnWidths || ConfigChanged || s_LayoutViewportWidth != Headers.w)
		aSizing = QmBrowserFitColumns(aSizing, ColumnBudget);
	else
		for(int i = 0; i < NumCols; ++i)
			aSizing[i] = s_aLayoutById[s_aCols[i].m_Id];
	if(HeaderInteractive && s_HeaderGesture.ResizeColumn() >= 0 && Ui()->CheckActiveItem(pResizeId))
	{
		aSizing = QmBrowserResizeColumns(s_aResizeStart, s_HeaderGesture.ResizeColumn(), Ui()->MouseX() - s_ResizePressX);
		// 所有受影响的列一起保存；取整只影响存档，当前拖动继续使用按下时的浮点快照。
		for(int i = 0; i < NumCols; ++i)
		{
			int *pConfig = GetColWidthConfig(s_aCols[i].m_Id);
			if(pConfig == nullptr)
				continue;
			const int Width = std::clamp((int)(aSizing[i].m_Width + 0.5f), (int)GetColMinWidth(s_aCols[i].m_Id), (int)GetColMaxWidth(s_aCols[i].m_Id));
			s_WidthsChanged |= *pConfig != Width;
			*pConfig = Width;
		}
	}
	// 保留精确显示宽度；松手后不因整数存档重新分配，内容测量也只影响本次按压。
	s_LayoutViewportWidth = Headers.w;
	float ColumnX = Headers.x;
	for(int i = 0; i < NumCols; ++i)
	{
		const int Id = s_aCols[i].m_Id;
		s_aLayoutById[Id] = aSizing[i];
		const int *pConfig = GetColWidthConfig(Id);
		s_aLayoutConfigById[Id] = pConfig != nullptr ? *pConfig : 0;
		s_aCols[i].m_Width = aSizing[i].m_Width;
		s_aCols[i].m_Rect = {ColumnX, Headers.y, aSizing[i].m_Width, Headers.h};
		ColumnX += aSizing[i].m_Width + ColGap;
	}
	const CUIRect ListView = View;

	struct SResizeHandle
	{
		CUIRect m_Rect;
		int m_ColIndex;
	};
	static std::vector<SResizeHandle> s_vResizeHandles;
	s_vResizeHandles.clear();

	Ui()->ClipEnable(&Headers);

	// 每个可调宽列的右缘提供同一条可见分隔线和命中区；图标列保持固定宽度。
	for(int i = NumPinnedCols; i + 1 < NumCols; i++)
	{
		const SColumn &Col = s_aCols[i];
		if(GetColWidthConfig(Col.m_Id) == nullptr)
			continue;
		bool CanResize = false;
		for(int Right = i + 1; Right < NumCols; ++Right)
			CanResize |= (aSizing[i].m_Width < aSizing[i].m_MaxWidth && aSizing[Right].m_Width > aSizing[Right].m_MinWidth) ||
				     (aSizing[i].m_Width > aSizing[i].m_MinWidth && aSizing[Right].m_Width < aSizing[Right].m_MaxWidth);
		if(!CanResize)
			continue;
		CUIRect Gap{Col.m_Rect.x + Col.m_Rect.w + ColGap * 0.5f - 4.0f, Col.m_Rect.y, 8.0f, Col.m_Rect.h};
		s_vResizeHandles.push_back({Gap, i});
	}

	int HoveredHandle = -1;
	float HoverDistance = std::numeric_limits<float>::max();
	for(int Index = 0; Index < (int)s_vResizeHandles.size(); ++Index)
	{
		const SResizeHandle &Handle = s_vResizeHandles[Index];
		const SColumn &Col = s_aCols[Handle.m_ColIndex];
		if(Col.m_Rect.w < 6.0f || !Ui()->MouseHovered(&Handle.m_Rect))
			continue;
		const float Distance = std::abs(Ui()->MouseX() - (Handle.m_Rect.x + Handle.m_Rect.w * 0.5f));
		if(Distance < HoverDistance)
		{
			HoverDistance = Distance;
			HoveredHandle = Index;
		}
	}

	if(HeaderInteractive && Ui()->MouseButtonClicked(0) && Ui()->ActiveItem() == nullptr)
	{
		int HeaderColumn = -1;
		for(int Index = NumPinnedCols; Index < NumCols; ++Index)
			if(Ui()->MouseHovered(&s_aCols[Index].m_Rect))
				HeaderColumn = Index;
		const int ResizeIndex = HoveredHandle >= 0 ? s_vResizeHandles[HoveredHandle].m_ColIndex : -1;
		s_HeaderGesture.Begin(HeaderColumn, ResizeIndex);
		if(ResizeIndex >= 0)
		{
			s_aResizeStart = aSizing;
			s_ResizePressX = Ui()->MouseX();
			s_ResizeViewportWidth = Headers.w;
			MeasureColumnContent = true;
			for(const auto &Col : s_aCols)
			{
				const char *pCaption = Col.m_Id == COL_QM_CLIENTS ? Col.m_pCaption : Localize(Col.m_pCaption);
				aContentWidths[Col.m_Id] = Col.m_Id == COL_FRIENDS ? 14.0f : TextRender()->TextWidth(Headers.h * CUi::ms_FontmodHeight, pCaption) + 10.0f;
			}
			Ui()->SetActiveItem(&s_aCols[ResizeIndex].m_Width);
		}
	}

	const bool PlayersOrPing = (g_Config.m_BrSort == IServerBrowser::SORT_NUMPLAYERS || g_Config.m_BrSort == IServerBrowser::SORT_PING);

	// 表头拖拽换列：按住内容列表头横移超过阈值进入换列（竖线指示落点），
	// 松开提交并持久化 br_col_order；未超过阈值的按压仍走普通的点击排序。
	constexpr float ReorderActivateDistance = 6.0f;

	auto ContentSlotFromMouseX = [&](float MouseXPos) {
		int Slot = 0;
		for(int i = NumPinnedCols; i < NumCols; i++)
		{
			if(i == s_ReorderColIndex)
				continue;
			if(MouseXPos > s_aCols[i].m_Rect.x + s_aCols[i].m_Rect.w * 0.5f)
				Slot++;
		}
		return Slot;
	};
	if(s_ReorderColIndex >= 0)
	{
		if(Ui()->MouseButton(0))
		{
			s_ReorderInsertSlot = ContentSlotFromMouseX(Ui()->MouseX());
			if(std::abs(Ui()->MouseX() - s_ReorderPressX) > ReorderActivateDistance)
				s_HeaderGesture.MarkReorderDrag();
		}
		else
		{
			if(s_HeaderGesture.ReorderDragged())
			{
				// 从当前顺序中摘除被拖列，插入落点槽位并持久化。
				const int Pos = s_ReorderColIndex - NumPinnedCols;
				int aNew[NumContentCols];
				int Out = 0;
				for(int i = 0; i < NumContentCols; i++)
					if(i != Pos)
						aNew[Out++] = s_aContentOrder[i];
				const int Slot = std::clamp(s_ReorderInsertSlot, 0, NumContentCols - 1);
				for(int i = NumContentCols - 1; i > Slot; i--)
					aNew[i] = aNew[i - 1];
				aNew[Slot] = s_aContentOrder[Pos];
				for(int i = 0; i < NumContentCols; i++)
					s_aContentOrder[i] = aNew[i];
				SerializeContentOrder();
			}
			s_ReorderColIndex = -1;
			s_ReorderInsertSlot = -1;
		}
	}

	// do headers
	int HeaderIndex = 0;
	for(const auto &Col : s_aCols)
	{
		// 拖拽换列的按压起点：内容列 + 无其他拖拽进行中。点击（未超阈值）
		// 不受影响，DoButton_GridHeader 正常返回；真正拖过的按压在下面抑制排序。
		if(s_ReorderColIndex < 0 && s_HeaderGesture.ReorderColumn() == HeaderIndex && Ui()->MouseButtonClicked(0))
		{
			s_ReorderColIndex = HeaderIndex;
			s_ReorderPressX = Ui()->MouseX();
			s_ReorderInsertSlot = ContentSlotFromMouseX(Ui()->MouseX());
		}

		Ui()->ClipEnable(&Col.m_Rect);
		int Checked = g_Config.m_BrSort == Col.m_Sort;
		if(PlayersOrPing && g_Config.m_BrSortOrder == 2 && (Col.m_Sort == IServerBrowser::SORT_NUMPLAYERS || Col.m_Sort == IServerBrowser::SORT_PING))
			Checked = 2;

		const char *pCaption = Col.m_Id == COL_QM_CLIENTS ? Col.m_pCaption : Localize(Col.m_pCaption);
		const bool BlockHeaderButton = s_HeaderGesture.ResizeColumn() >= 0 || (HoveredHandle >= 0 && s_HeaderGesture.ReorderColumn() < 0);
		if(BlockHeaderButton)
			Ui()->BeginBackgroundRender(true);
		const bool HeaderClicked = DoButton_GridHeader(&Col.m_Id, pCaption, Checked, &Col.m_Rect, g_Config.m_BrSortOrder != 0, TEXTALIGN_ML, QmBrowserTableLabelProperties(Col.m_Rect.w));
		if(BlockHeaderButton)
			Ui()->EndBackgroundRender(true);
		if(HeaderClicked)
		{
			// 这次按压演变成了拖拽换列：吞掉点击，不触发排序。
			if(Col.m_Sort != -1 && s_HeaderGesture.AllowsSort())
			{
				if(g_Config.m_BrSort == Col.m_Sort)
					g_Config.m_BrSortOrder = (g_Config.m_BrSortOrder + 1) % (PlayersOrPing ? 3 : 2);
				else
					g_Config.m_BrSortOrder = 0;
				g_Config.m_BrSort = Col.m_Sort;
			}
		}
		HeaderIndex++;

		if(Col.m_Id == COL_FRIENDS)
		{
			GameClient()->m_Tooltips.DoToolTip(&Col.m_Id, &Col.m_Rect, Localize("Sort by number of friends"));
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
			Ui()->DoLabel_QmIcon(&Col.m_Rect, EQmIcon::HEART, FONT_ICON_HEART, 14.0f, TEXTALIGN_MC);
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}
		else if(Col.m_Id == COL_FLAG_FAV)
		{
			GameClient()->m_Tooltips.DoToolTip(&Col.m_Id, &Col.m_Rect, Localize("Sort by favorites"));
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
			Ui()->DoLabel_QmIcon(&Col.m_Rect, EQmIcon::STAR, FONT_ICON_STAR, 14.0f, TEXTALIGN_MC);
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}
		else if(Col.m_Id == COL_QM_CLIENTS)
		{
			GameClient()->m_Tooltips.DoToolTip(&Col.m_Id, &Col.m_Rect, Localize("Sort by number of QmClient players"));
		}
		Ui()->ClipDisable();
	}

	// 换列落点指示：亮色竖线标出松开后的插入边界；虚影标题跟随鼠标，
	// 明示正在拖的是哪一列。
	if(s_ReorderColIndex >= 0 && s_HeaderGesture.ReorderDragged())
	{
		float LineX = s_aCols[NumCols - 1].m_Rect.x + s_aCols[NumCols - 1].m_Rect.w + 1.0f;
		int Seen = 0;
		for(int i = NumPinnedCols; i < NumCols; i++)
		{
			if(i == s_ReorderColIndex)
				continue;
			if(Seen == s_ReorderInsertSlot)
			{
				LineX = s_aCols[i].m_Rect.x - 1.0f;
				break;
			}
			Seen++;
		}
		CUIRect Indicator{LineX - 1.0f, Headers.y, 3.0f, Headers.h};
		Indicator.Draw(BrowserOpacityColor(ColorRGBA(1.0f, 0.85f, 0.35f, 0.9f)), IGraphics::CORNER_NONE, 0.0f);

		Ui()->ClipDisable();
		const SColumn &DragCol = s_aCols[s_ReorderColIndex];
		const char *pDragCaption = DragCol.m_Id == COL_QM_CLIENTS ? DragCol.m_pCaption : Localize(DragCol.m_pCaption);
		const float GhostFontSize = Headers.h * CUi::ms_FontmodHeight;
		const float GhostTextWidth = TextRender()->TextWidth(GhostFontSize, pDragCaption, -1, -1.0f);
		const float ScreenW = Ui()->Screen()->w;
		const float GhostX = std::clamp(Ui()->MouseX() + 12.0f, 4.0f, std::max(4.0f, ScreenW - GhostTextWidth - 34.0f));
		CUIRect Ghost{GhostX, Ui()->MouseY() - Headers.h * 0.5f - 3.0f, GhostTextWidth + 22.0f, Headers.h + 6.0f};
		Ghost.Draw(BrowserOpacityColor(ColorRGBA(0.09f, 0.11f, 0.15f, 0.94f)), IGraphics::CORNER_ALL, 5.0f);
		Ghost.VMargin(11.0f, &Ghost);
		TextRender()->TextColor(BrowserOpacityColor(ColorRGBA(0.95f, 0.96f, 0.98f, 1.0f)));
		Ui()->DoLabel(&Ghost, pDragCaption, GhostFontSize, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Ui()->ClipEnable(&Headers);
	}

	const bool Resizing = s_HeaderGesture.ResizeColumn() >= 0;
	const bool ResizeHover = HoveredHandle >= 0 && s_HeaderGesture.ReorderColumn() < 0 && Ui()->ActiveItem() == nullptr;
	m_BrowserColumnResizeCursor = HeaderInteractive && (Resizing || ResizeHover);
	if(ResizeHover)
		Ui()->SetHotItem(&s_aCols[s_vResizeHandles[HoveredHandle].m_ColIndex].m_Width);

	if(HeaderInteractive && !Ui()->MouseButton(0))
	{
		if(Resizing && Ui()->IsActiveItem(&s_aCols[s_HeaderGesture.ResizeColumn()].m_Width))
			Ui()->SetActiveItem(nullptr);
		// 所有表头消费完松开帧后再结束，调宽和换列都不会误触排序。
		s_HeaderGesture.End();
		if(s_WidthsChanged)
		{
			ConfigManager()->Save();
			s_WidthsChanged = false;
		}
	}

	for(const auto &Handle : s_vResizeHandles)
	{
		const int ColIdx = Handle.m_ColIndex;
		bool Hovered = ResizeHover && s_vResizeHandles[HoveredHandle].m_ColIndex == ColIdx;
		bool Dragging = s_HeaderGesture.ResizeColumn() == ColIdx;

		float LineX = Handle.m_Rect.x + Handle.m_Rect.w * 0.5f;

		float Alpha = 0.15f;
		float LineW = 1.0f;
		if(Hovered)
		{
			Alpha = 0.5f;
			LineW = 2.0f;
		}
		if(Dragging)
		{
			Alpha = 1.0f;
			LineW = 2.0f;
		}

		CUIRect Line;
		Line.x = LineX - LineW * 0.5f;
		Line.y = s_aCols[ColIdx].m_Rect.y;
		Line.w = LineW;
		Line.h = s_aCols[ColIdx].m_Rect.h;
		Line.Draw(BrowserOpacityColor(ColorRGBA(1.0f, 1.0f, 1.0f, Alpha)), IGraphics::CORNER_NONE, 0.0f);
	}

	Ui()->ClipDisable();

	const int64_t Now = time_get();
	const bool NeedDifficultyForList = g_Config.m_QmMapBrowserStarMask || g_Config.m_QmMapBrowserCategoryMask || g_Config.m_BrSort == IServerBrowser::SORT_DIFFICULTY;
	auto RequestDifficulty = [&](const CServerInfo &Server) {
		if(!Ui()->RenderOnly())
			m_MapDifficultyCatalog.RequestAxiomDifficulty(Server, *Http(), GameClient()->m_QmAxiomScores.OpenApiBudget(), g_Config.m_QmAxiomApiKey, Now);
	};
	std::vector<int> vVisibleServerIndices;
	vVisibleServerIndices.reserve(ServerBrowser()->NumSortedServers());
	for(int SortedIndex = 0; SortedIndex < ServerBrowser()->NumSortedServers(); ++SortedIndex)
	{
		const CServerInfo *pServer = ServerBrowser()->SortedGet(SortedIndex);
		if(pServer == nullptr)
			continue;
		const CQmMapDifficultyCatalog::SEntry *pDifficulty = m_MapDifficultyCatalog.FindForServer(*pServer, MapCategoryHintFromServer(pServer));
		const int Stars = pDifficulty != nullptr ? pDifficulty->m_Stars : -1;
		const bool IsFavorite = GameClient()->m_TClient.IsFavoriteMap(pServer->m_aMap);
		// 星级筛选会隐藏未知地图，需在过滤前补查，否则这些图永远无法进入可见行。
		if(NeedDifficultyForList && !pDifficulty && (!g_Config.m_QmMapBrowserEmptyOnly || !pServer->m_NumFilteredPlayers) &&
			(!g_Config.m_QmMapBrowserFavoriteOnly || IsFavorite))
			RequestDifficulty(*pServer);
		// 目录条目给出权威类别；没有目录收录时退回服务器名/社区类型里解析出的类别。
		const char *pCategory = pDifficulty != nullptr ? pDifficulty->m_Category.c_str() : MapCategoryHintFromServer(pServer);
		if(QmMapVotes::MatchesFilter(Stars, pCategory, pServer->m_NumFilteredPlayers, g_Config.m_QmMapBrowserEmptyOnly != 0, g_Config.m_QmMapBrowserStarMask, g_Config.m_QmMapBrowserCategoryMask, g_Config.m_QmMapBrowserFavoriteOnly != 0, IsFavorite))
			vVisibleServerIndices.push_back(SortedIndex);
	}
	const int NumServers = (int)vVisibleServerIndices.size();

	// display important messages in the middle of the screen so no
	// users misses it
	{
		// 局域网标签不查询主服务器，后台 HTTP 刷新（如好友在线扫描）时不应显示主服务器加载提示
		const bool IsLanTab = ServerBrowser()->GetCurrentType() == IServerBrowser::TYPE_LAN;
		if(!ServerBrowser()->NumServers() && !IsLanTab && ServerBrowser()->IsGettingServerlist())
		{
			char aLoadingLabel[256];
			const int LoadingDotsCount = static_cast<int>(Client()->GlobalTime() * 3.0f) % 7;
			str_format(aLoadingLabel, sizeof(aLoadingLabel), "%s%.*s", Localize("Getting server list from master server"), LoadingDotsCount, "......");
			Ui()->DoLabel(&View, aLoadingLabel, 16.0f, TEXTALIGN_MC);
		}
		else if(!ServerBrowser()->NumServers())
		{
			if(IsLanTab)
			{
				CUIRect Label, Button;
				View.HMargin((View.h - (16.0f + 18.0f + 8.0f)) / 2.0f, &Label);
				Label.HSplitTop(16.0f, &Label, &Button);
				Button.HSplitTop(8.0f, nullptr, &Button);
				Button.VMargin((Button.w - 320.0f) / 2.0f, &Button);
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), Localize("No local servers found (ports %d-%d)"), IServerBrowser::LAN_PORT_BEGIN, IServerBrowser::LAN_PORT_END);
				Ui()->DoLabel(&Label, aBuf, 16.0f, TEXTALIGN_MC);
				static CButtonContainer s_StartLocalServerButton;
				if(DoButton_Menu(&s_StartLocalServerButton, Localize("Start and connect to local server"), 0, &Button))
				{
					if(GameClient()->m_LocalServer.IsServerRunning())
					{
						RefreshBrowserTab(true);
						GameClient()->m_LocalServer.Connect();
					}
					else
					{
						GameClient()->m_LocalServer.RunServer({}, true);
					}
				}
			}
			else if(ServerBrowser()->IsServerlistError())
			{
				Ui()->DoLabel(&View, Localize("Could not get server list from master server"), 16.0f, TEXTALIGN_MC);
			}
			else
			{
				Ui()->DoLabel(&View, Localize("No servers found"), 16.0f, TEXTALIGN_MC);
			}
		}
		else if(ServerBrowser()->NumServers() && !NumServers)
		{
			CUIRect Label, ResetButton;
			View.HMargin((View.h - (16.0f + 18.0f + 8.0f)) / 2.0f, &Label);
			Label.HSplitTop(16.0f, &Label, &ResetButton);
			ResetButton.HSplitTop(8.0f, nullptr, &ResetButton);
			ResetButton.VMargin((ResetButton.w - 200.0f) / 2.0f, &ResetButton);
			Ui()->DoLabel(&Label, Localize("No servers match your filter criteria"), 16.0f, TEXTALIGN_MC);
			static CButtonContainer s_ResetButton;
			if(DoButton_Menu(&s_ResetButton, Localize("Reset filter"), 0, &ResetButton))
			{
				ResetServerbrowserFilters();
			}
		}
	}

	s_ListBox.SetActive(!Ui()->IsPopupOpen());
	const bool PerfListFrameEnabled = QmPerfEnabled();
	const auto ListFrameStartTime = PerfListFrameEnabled ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();
	int SelectedServerIndex = -1;
	for(int VisibleIndex = 0; VisibleIndex < NumServers; ++VisibleIndex)
	{
		const CServerInfo *pServer = ServerBrowser()->SortedGet(vVisibleServerIndices[VisibleIndex]);
		if(pServer != nullptr && str_comp(pServer->m_aAddress, g_Config.m_UiServerAddress) == 0)
		{
			SelectedServerIndex = VisibleIndex;
			break;
		}
	}
	// 列表行和滚动条保留原有淡化，不影响上方表头及空列表提示中的按钮。
	CUiBackgroundAlphaScaleScope ListOpacityScope(Ui(), SERVER_LIST_SCROLLBAR_RAIL_ALPHA_SCALE);
	// 胶囊行：整行圆角、主题选中/悬浮填充与行内左右内退。行距已经并入
	// BrowserRowStep，这里显式把组件行距压成 0——可见范围裁剪只用 SkipItems，
	// 它不补行距，滚过一屏后整张表会向上漂移。行间留白由行内选择内缩给出。
	// 行反馈（选中/悬浮胶囊）一律传主题选中色原色：列表反馈只看强调色，悬浮与
	// 选中是同一份 m_Selected（与下拉弹层同源），不要再掺白色 token 另起一档。
	// 上面那次整体淡化只服务滚动条轨道，反算已经下沉到 CListBox 内部（组件按
	// 1/BackgroundAlphaScale 还原一次）；这里再预除一遍会变成二次补偿。
	const ColorRGBA ThemeSelected = Ui()->QmControlTheme().m_Selected;
	s_ListBox.SetItemColors(ThemeSelected, ThemeSelected, ThemeSelected);
	s_ListBox.DoAutoSpacing(0.0f);
	s_ListBox.SetItemSelectionInset(ui_token::list::ROW_SPACING * 0.5f);
	// 可见行取整：按实际可用高度收敛到整数行，首/末行不会被裁掉半行。
	// 容器内边距（显式能力）：服务器列表也要首/末行与容器边缘留白。
	s_ListBox.SetContainerPadding(ui_token::list::ContainerPadding(BrowserRowStep));
	const int ServerVisibleRows = maximum(1, (int)std::floor(std::max(0.0f, View.h - 2.0f * ui_token::list::ContainerPadding(BrowserRowStep)) / BrowserRowStep));
	s_ListBox.SetVisibleRowRange(1, ServerVisibleRows);
	s_ListBox.DoStart(BrowserRowStep, NumServers, 1, 3, SelectedServerIndex, &View, false);

	const bool RevealSelection = m_ServerBrowserShouldRevealSelection;
	if(m_ServerBrowserShouldRevealSelection)
	{
		s_ListBox.ScrollToSelected();
		m_ServerBrowserShouldRevealSelection = false;
	}
	m_SelectedIndex = SelectedServerIndex;

	// 列表框仍需为所有项目维护滚动几何，但只有可见行需要命中测试、文本布局和绘制命令。
	// 两侧各保留一行，避免滚轮/滚动动画结算时出现空行；显式要求显示选中项时保留全量遍历，
	// 让 DoNextItem 继续执行原有的选中项定位逻辑。
	int FirstVisibleItem = 0;
	int EndVisibleItem = NumServers;
	if(!RevealSelection && NumServers > 0)
	{
		const float ScrollY = std::max(0.0f, s_ListBox.ScrollOffsetY());
		const float ViewHeight = std::max(0.0f, s_ListBox.ViewHeight());
		const int ExtraRows = 1;
		const int FirstVisibleRow = std::max(0, (int)std::floor(ScrollY / BrowserRowStep) - ExtraRows);
		const int LastVisibleRowExclusive = std::min(
			NumServers,
			(int)std::ceil((ScrollY + ViewHeight) / BrowserRowStep) + ExtraRows);
		FirstVisibleItem = std::min(FirstVisibleRow, NumServers);
		EndVisibleItem = std::max(FirstVisibleItem, LastVisibleRowExclusive);
	}
	if(FirstVisibleItem > 0)
		s_ListBox.SkipItems(FirstVisibleItem);

	const auto &&RenderBrowserIcons = [this](CUIElement::SUIElementRect &UIRect, CUIRect *pRect, const ColorRGBA &TextColor, const ColorRGBA &TextOutlineColor, EQmIcon Icon, const char *pText, int TextAlign, float FontSize = 14.0f) {
		TextRender()->TextColor(TextColor);
		TextRender()->TextOutlineColor(TextOutlineColor);
		if(Icon != EQmIcon::COUNT)
		{
			// 图集优先；pText 为回退字形。
			Ui()->DoLabel_QmIcon(pRect, Icon, pText, FontSize, TextAlign);
		}
		else
		{
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
			Ui()->DoLabelStreamed(UIRect, pRect, pText, FontSize, TextAlign);
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}
		TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	};

	std::vector<CUIElement *> &vpServerBrowserUiElements = m_avpServerBrowserUiElements[ServerBrowser()->GetCurrentType()];
	// 按服务器稳定索引保存文本容器，而不是按排序后位置保存。排序按延迟/玩家
	// 数变化时，位置会频繁交换；按位置缓存会让滚动每帧重建可见行的文字。
	const int ServerCacheSize = maximum(ServerBrowser()->NumServers(), NumServers);
	if(vpServerBrowserUiElements.size() < (size_t)ServerCacheSize)
		vpServerBrowserUiElements.resize(ServerCacheSize, nullptr);

	const auto MeasureColumn = [&](int ColumnId, const char *pText, float FontSize, float Padding = 0.0f) {
		if(MeasureColumnContent)
			aContentWidths[ColumnId] = std::max(aContentWidths[ColumnId], TextRender()->TextWidth(FontSize, pText) + Padding);
	};

	int RowsVisible = 0;
	int RowsRendered = 0;
	int RowsIterated = 0;
	for(int i = FirstVisibleItem; i < EndVisibleItem; i++)
	{
		const CServerInfo *pItem = ServerBrowser()->SortedGet(vVisibleServerIndices[i]);
		RowsIterated += PerfListFrameEnabled ? 1 : 0;

		const CListboxItem ListItem = s_ListBox.DoNextItem(pItem, str_comp(pItem->m_aAddress, g_Config.m_UiServerAddress) == 0);
		if(ListItem.m_Selected)
			m_SelectedIndex = i;

		if(!ListItem.m_Visible)
		{
			// reset active item, if not visible
			if(Ui()->CheckActiveItem(pItem))
				Ui()->SetActiveItem(nullptr);

			// don't render invisible items
			continue;
		}

		// QmClient: 可见行补测延迟。没有可用实测值的行在浏览时排队发一次 GETINFO，
		// 几秒内把地区估算换成真实数值；结果写入 ping 缓存，之后打开直接显示。
		// 同一行重复调用由 RequestServerPing 的队列/失败/同 IP 窗口判断去重。
		ServerBrowser()->RequestServerPing(pItem->m_aAddresses[0]);
		if(!NeedDifficultyForList)
			RequestDifficulty(*pItem);

		// Resolve row-only data after visibility is known. The list can contain
		// hundreds of servers while only a few rows are on screen; doing these
		// lookups and allocating the streamed UI element for every hidden row
		// made the server browser pay an unnecessary per-frame cost.
		const CCommunity *pCommunity = ServerBrowser()->Community(pItem->m_aCommunityId);
		const int CacheIndex = pItem->m_ServerIndex >= 0 && pItem->m_ServerIndex < ServerCacheSize ? pItem->m_ServerIndex : vVisibleServerIndices[i];
		if(vpServerBrowserUiElements[CacheIndex] == nullptr)
			vpServerBrowserUiElements[CacheIndex] = Ui()->GetNewUIElement(NUM_UI_ELEMS);
		CUIElement *pUiElement = vpServerBrowserUiElements[CacheIndex];

		if(PerfListFrameEnabled)
		{
			RowsVisible++;
			RowsRendered++;
		}

		const float FontSize = SERVER_LIST_TEXT_SIZE;
		char aTemp[64];
		// 短名仅对国服生效（用户校准：默认开、只对国服）。国服判定与可见行
		// 补测共用 ServerBrowser()->IsChineseServer，避免两处口径分叉。
		const bool ShortNameAllowed = ServerBrowser()->IsChineseServer(*pItem);
		char aDisplayServerName[sizeof(pItem->m_aName)];
		char aParsedShortName[sizeof(pItem->m_aName)];
		const char *pDisplayServerName = pItem->m_aName;
		if(g_Config.m_QmShortServerNames && ShortNameAllowed)
		{
			CMenus::GetServerbrowserDisplayName(pItem, aParsedShortName, sizeof(aParsedShortName));
			// 名称列只显示区段（「简单 - CHN5 上海」→「CHN5 上海」）。
			pDisplayServerName = BrowserSegmentDisplayName(aParsedShortName, aDisplayServerName, sizeof(aDisplayServerName));
			// 解析结果剥前缀后为空白（名称全空白或仅含空白段）时回退原始名称，
			// 避免名称列渲染出空文本容器。
			bool SegmentBlank = true;
			for(const char *p = pDisplayServerName; *p != '\0'; ++p)
			{
				if(*p != ' ' && *p != '\t')
				{
					SegmentBlank = false;
					break;
				}
			}
			if(SegmentBlank)
				pDisplayServerName = pItem->m_aName;
		}
		// 行内容排在内退后的内容区上：左右内边距由 CListBox 的胶囊行给出，
		// 表格列只在这个内容区里按列宽切分，不再用整行矩形贴边摆文字。
		// 纵向再让出半档，让内容与行胶囊（不含格间缝隙）同高。
		CUIRect RowContent = ListItem.m_ContentRect;
		RowContent.HMargin(ui_token::list::ROW_SPACING * 0.5f, &RowContent);
		const CUIRect RowClip{Headers.x, RowContent.y, Headers.w, RowContent.h};
		Ui()->ClipEnable(&RowClip);
		for(const auto &Col : s_aCols)
		{
			if(Col.m_Rect.x + Col.m_Rect.w <= Headers.x || Col.m_Rect.x >= Headers.x + Headers.w)
				continue;
			CUIRect Button;
			Button.x = Col.m_Rect.x;
			Button.y = RowContent.y;
			Button.h = RowContent.h;
			Button.w = Col.m_Rect.w;
			if(Button.w <= 0.0f)
				continue;
			Ui()->ClipEnable(&Button);

			const int Id = Col.m_Id;
			if(Id == COL_FLAG_LOCK)
			{
				if(pItem->m_Flags & SERVER_FLAG_PASSWORD)
				{
					RenderBrowserIcons(*pUiElement->Rect(UI_ELEM_LOCK_ICON), &Button, ColorRGBA(0.75f, 0.75f, 0.75f, 1.0f), TextRender()->DefaultTextOutlineColor(), EQmIcon::LOCK, FONT_ICON_LOCK, TEXTALIGN_MC);
				}
				else if(pItem->m_RequiresLogin)
				{
					RenderBrowserIcons(*pUiElement->Rect(UI_ELEM_KEY_ICON), &Button, ColorRGBA(0.75f, 0.75f, 0.75f, 1.0f), TextRender()->DefaultTextOutlineColor(), EQmIcon::KEY, FONT_ICON_KEY, TEXTALIGN_MC);
				}
			}
			else if(Id == COL_FLAG_FAV)
			{
				// 进场表只在「刚点了收藏」时创建条目（由 gs_FavoriteStarGrowPending 登记），
				// 落位后立即删除；因此表里只有正在进场的星标，不再有墓碑与无界增长。
				static std::map<std::string, float, std::less<>> s_StarGrow;
				if(pItem->m_Favorite != TRISTATE::NONE)
				{
					auto PendingIt = gs_FavoriteStarGrowPending.find(pItem->m_aAddress);
					if(PendingIt != gs_FavoriteStarGrowPending.end())
					{
						s_StarGrow[pItem->m_aAddress] = 0.0f;
						gs_FavoriteStarGrowPending.erase(PendingIt);
					}
					auto StarIt = s_StarGrow.find(pItem->m_aAddress);
					if(StarIt == s_StarGrow.end())
					{
						// 没有进场请求（启动时就已收藏、或刚播完）：按完成态绘制。
						RenderBrowserIcons(*pUiElement->Rect(UI_ELEM_FAVORITE_ICON), &Button, ColorRGBA(1.0f, 0.85f, 0.3f, 1.0f), TextRender()->DefaultTextOutlineColor(), EQmIcon::STAR, FONT_ICON_STAR, TEXTALIGN_MC);
					}
					else
					{
						if(StarIt->second < 1.0f && !Ui()->MeasurementOnly())
						{
							// 按真实帧间隔推进（每帧只采样一次时钟）；测量通道不推进，
							// 否则同帧多次遍历会把 0.35s 的进场压缩掉。
							static int64_t s_StarDtTick = 0;
							static float s_StarDt = 1.0f / 60.0f;
							const int64_t StarNowTick = time_get();
							if(StarNowTick != s_StarDtTick)
							{
								s_StarDt = s_StarDtTick == 0 ? 1.0f / 60.0f : std::clamp((float)(StarNowTick - s_StarDtTick) / (float)maximum<int64_t>(1, time_freq()), 1.0f / 1000.0f, 0.1f);
								s_StarDtTick = StarNowTick;
							}
							const float Step = g_Config.m_QmUiMotionLevel > 0 ? s_StarDt / 0.35f : 1.0f;
							StarIt->second = std::min(1.0f, StarIt->second + Step);
						}
						const float StarGrow = std::clamp(StarIt->second, 0.0f, 1.0f);
						if(StarGrow >= 1.0f)
						{
							// 落位后回到共享绘制路径，像素与静态状态完全一致，并清掉条目。
							RenderBrowserIcons(*pUiElement->Rect(UI_ELEM_FAVORITE_ICON), &Button, ColorRGBA(1.0f, 0.85f, 0.3f, 1.0f), TextRender()->DefaultTextOutlineColor(), EQmIcon::STAR, FONT_ICON_STAR, TEXTALIGN_MC);
							s_StarGrow.erase(StarIt);
						}
						else
						{
							// 生长曲线铺满整段时长：三次缓出 + 中段轻微鼓起，落位回到 1.0。
							const float StarEaseOut = 1.0f - std::pow(1.0f - StarGrow, 3.0f);
							const float StarBulge = 1.0f + 0.10f * std::sin(3.14159265f * StarGrow);
							const float StarScale = (0.25f + 0.75f * StarEaseOut) * StarBulge;
							const float StarAlpha = std::clamp(0.15f + 0.85f * StarGrow, 0.0f, 1.0f);
							// 字形等比缩放：QmIconLabelRect 的边长由 Size 决定，字体度量跟着盒子走。
							const float IconSize = std::clamp(14.0f * StarScale, 2.0f, 22.0f);
							// 只改这一帧的文本色，退出时恢复进入前颜色，避免污染后续列。
							const ColorRGBA PreviousTextColor = TextRender()->GetTextColor();
							TextRender()->TextColor(ColorRGBA(1.0f, 0.85f, 0.3f, StarAlpha));
							Ui()->DoLabel_QmIcon(&Button, EQmIcon::STAR, FONT_ICON_STAR, IconSize, TEXTALIGN_MC);
							TextRender()->TextColor(PreviousTextColor);
						}
					}
				}
				else
				{
					// 取消收藏：丢弃未完成的进场条目。
					auto StarIt = s_StarGrow.find(pItem->m_aAddress);
					if(StarIt != s_StarGrow.end())
						s_StarGrow.erase(StarIt);
				}
			}
			else if(Id == COL_COMMUNITY)
			{
				if(pCommunity != nullptr)
				{
					const CCommunityIcon *pIcon = m_CommunityIcons.Find(pCommunity->Id());
					if(pIcon != nullptr)
					{
						CUIRect CommunityIcon;
						Button.Margin(2.0f, &CommunityIcon);
						m_CommunityIcons.Render(pIcon, CommunityIcon, true, Ui()->ControlAlphaScale());
						Ui()->DoButtonLogic(&pItem->m_aCommunityId, 0, &CommunityIcon, BUTTONFLAG_NONE);
						GameClient()->m_Tooltips.DoToolTip(&pItem->m_aCommunityId, &CommunityIcon, pCommunity->Name());
					}
				}
			}
			else if(Id == COL_NAME)
			{
				MeasureColumn(Id, pDisplayServerName, FontSize);
				SLabelProperties Props;
				Props.m_MaxWidth = Button.w;
				Props.m_StopAtEnd = true;
				Props.m_EnableWidthCheck = false;
				// 名称列可能被搜索高亮拆成多段流式绘制；裁剪时统一用完整名称提示。
				Props.m_pTooltipId = pUiElement->Rect(UI_ELEM_NAME_1);
				Props.m_pTooltipText = pDisplayServerName;

				bool Printed = false;
				if(g_Config.m_BrFilterString[0] && (g_Config.m_QmShortServerNames || (pItem->m_QuickSearchHit & IServerBrowser::QUICK_SERVERNAME)))
					Printed = PrintHighlighted(pDisplayServerName, [&](const char *pFilteredStr, const int FilterLen) {
						Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_NAME_1), &Button, pDisplayServerName, FontSize, TEXTALIGN_ML, Props, (int)(pFilteredStr - pDisplayServerName));
						TextRender()->TextColor(gs_HighlightedTextColor);
						Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_NAME_2), &Button, pFilteredStr, FontSize, TEXTALIGN_ML, Props, FilterLen, &pUiElement->Rect(UI_ELEM_NAME_1)->m_Cursor);
						TextRender()->TextColor(TextRender()->DefaultTextColor());
						Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_NAME_3), &Button, pFilteredStr + FilterLen, FontSize, TEXTALIGN_ML, Props, -1, &pUiElement->Rect(UI_ELEM_NAME_2)->m_Cursor);
					});
				if(!Printed)
					Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_NAME_1), &Button, pDisplayServerName, FontSize, TEXTALIGN_ML, Props);
			}
			else if(Id == COL_DIFFICULTY)
			{
				// 独立难度列显示类别与星级；无目录条目显示占位。
				const CQmMapDifficultyCatalog::SEntry *pDifficulty = m_MapDifficultyCatalog.FindForServer(*pItem, MapCategoryHintFromServer(pItem));
				char aDifficulty[128];
				FormatMapDifficultyText(*pItem, pDifficulty, aDifficulty, sizeof(aDifficulty));
				MeasureColumn(Id, aDifficulty[0] != '\0' ? aDifficulty : "--", FontSize);
				if(aDifficulty[0] != '\0')
				{
					SLabelProperties Props;
					Props.m_MaxWidth = Button.w;
					Props.m_StopAtEnd = true;
					Props.m_EllipsisAtEnd = true;
					Props.m_EnableWidthCheck = false;
					Props.m_pTooltipId = pUiElement->Rect(UI_ELEM_DIFFICULTY);
					Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_DIFFICULTY), &Button, aDifficulty, FontSize, TEXTALIGN_ML, Props);
				}
				else
				{
					// 未收录地图：灰色占位与有难度的行明确区分。
					TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
					TextRender()->TextColor(TextRender()->DefaultTextColor().WithAlpha(0.45f));
					SLabelProperties Props;
					Props.m_MaxWidth = Button.w;
					Props.m_StopAtEnd = true;
					Props.m_EnableWidthCheck = false;
					Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_DIFFICULTY), &Button, "--", FontSize, TEXTALIGN_ML, Props);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
			}
			else if(Id == COL_GAMETYPE)
			{
				MeasureColumn(Id, pItem->m_aGameType, FontSize);
				SLabelProperties Props;
				Props.m_MaxWidth = Button.w;
				Props.m_StopAtEnd = true;
				Props.m_EnableWidthCheck = false;
				Props.m_pTooltipId = pUiElement->Rect(UI_ELEM_GAMETYPE);
				if(g_Config.m_UiColorizeGametype)
				{
					TextRender()->TextColor(pItem->m_GametypeColor);
				}
				Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_GAMETYPE), &Button, pItem->m_aGameType, FontSize, TEXTALIGN_ML, Props);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			else if(Id == COL_MAP)
			{
				MeasureColumn(Id, pItem->m_aMap, FontSize, 8.0f + 2.0f * Button.h);
				{
					CUIRect Icon;
					Button.VMargin(std::min(4.0f, std::max(0.0f, Button.w) * 0.5f), &Button);
					// 地图预览入口：地图名前的可点击小图标（点击开 / 再点关，
					// 点击型面板无悬停触发）。按钮 id 取行内未占用字段地址。
					Button.VSplitLeft(std::min(std::max(0.0f, Button.w), std::max(0.0f, Button.h)), &Icon, &Button);
					if(Icon.w > 4.0f)
					{
						CUIRect PreviewIcon = Icon;
						PreviewIcon.Margin(2.0f, &PreviewIcon);
						// 同步放大绘制区域，避免图标被原有边距限制尺寸。
						PreviewIcon.Margin(-0.1f * std::min(PreviewIcon.w, PreviewIcon.h), &PreviewIcon);
						const bool SamePreviewTarget = Ui()->IsPopupOpen(&m_MapPreviewPopupContext) &&
									       str_comp(m_MapPreviewPopupContext.m_Target.m_aMapName, pItem->m_aMap) == 0;
						if(Ui()->DoButtonLogic(&pItem->m_RequiresLogin, 0, &PreviewIcon, BUTTONFLAG_LEFT))
						{
							if(!SamePreviewTarget)
								OpenMapPreviewPopup(pItem);
						}
						const bool IconHot = SamePreviewTarget || Ui()->HotItem() == &pItem->m_RequiresLogin;
						RenderBrowserIcons(*pUiElement->Rect(UI_ELEM_MAP_PREVIEW_ICON), &PreviewIcon,
							IconHot ? TextRender()->DefaultTextColor() : TextRender()->DefaultTextColor().WithAlpha(0.75f),
							TextRender()->DefaultTextOutlineColor(), EQmIcon::SEARCH, FONT_ICON_MAGNIFYING_GLASS, TEXTALIGN_MC, 14.0f * 1.2f);
						GameClient()->m_Tooltips.DoToolTip(&pItem->m_RequiresLogin, &PreviewIcon, Localize("Toggle map preview"));
					}
					// 完赛旗槽位紧随预览图标之后（条件显示，无旗时留白一行宽）。
					Button.VSplitLeft(std::min(std::max(0.0f, Button.w), std::max(0.0f, Button.h)), &Icon, &Button);
					if(Icon.w > 4.0f && g_Config.m_BrIndicateFinished && pItem->m_HasRank == CServerInfo::RANK_RANKED)
					{
						Icon.Margin(2.0f, &Icon);
						RenderBrowserIcons(*pUiElement->Rect(UI_ELEM_FINISH_ICON), &Icon, TextRender()->DefaultTextColor(), TextRender()->DefaultTextOutlineColor(), EQmIcon::FLAG_CHECKERED, FONT_ICON_FLAG_CHECKERED, TEXTALIGN_MC);
					}
				}

				if(Button.w > 0.0f)
				{
					// 检查是否是收藏地图
					const bool IsFavoriteMap = GameClient()->m_TClient.IsFavoriteMap(pItem->m_aMap);
					if(IsFavoriteMap)
						TextRender()->TextColor(ui_token::color::FAVORITE);
					SLabelProperties Props;
					Props.m_MaxWidth = Button.w;
					Props.m_StopAtEnd = true;
					Props.m_EnableWidthCheck = false;
					Props.m_pTooltipId = pUiElement->Rect(UI_ELEM_MAP_1);
					Props.m_pTooltipText = pItem->m_aMap;
					bool Printed = false;
					if(g_Config.m_BrFilterString[0] && (pItem->m_QuickSearchHit & IServerBrowser::QUICK_MAPNAME))
						Printed = PrintHighlighted(pItem->m_aMap, [&](const char *pFilteredStr, const int FilterLen) {
							Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_MAP_1), &Button, pItem->m_aMap, FontSize, TEXTALIGN_ML, Props, (int)(pFilteredStr - pItem->m_aMap));
							TextRender()->TextColor(gs_HighlightedTextColor);
							Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_MAP_2), &Button, pFilteredStr, FontSize, TEXTALIGN_ML, Props, FilterLen, &pUiElement->Rect(UI_ELEM_MAP_1)->m_Cursor);
							TextRender()->TextColor(TextRender()->DefaultTextColor());
							Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_MAP_3), &Button, pFilteredStr + FilterLen, FontSize, TEXTALIGN_ML, Props, -1, &pUiElement->Rect(UI_ELEM_MAP_2)->m_Cursor);
						});
					if(!Printed)
						Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_MAP_1), &Button, pItem->m_aMap, FontSize, TEXTALIGN_ML, Props);

					const char *pMapNote = GameClient()->m_TClient.GetMapNote(pItem->m_aMap);
					if(pMapNote && pMapNote[0] != '\0' && Ui()->MouseHovered(&Button))
					{
						static char s_aMapNoteTooltip[512];
						str_format(s_aMapNoteTooltip, sizeof(s_aMapNoteTooltip), "%s: %s", Localize("Note"), pMapNote);
						Ui()->DoButtonLogic(&pItem->m_aMap, 0, &Button, BUTTONFLAG_NONE);
						GameClient()->m_Tooltips.DoToolTip(&pItem->m_aMap, &Button, s_aMapNoteTooltip, 320.0f);
					}

					if(IsFavoriteMap)
						TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
			}
			else if(Id == COL_FRIENDS)
			{
				if(pItem->m_FriendState != IFriends::FRIEND_NO)
				{
					RenderBrowserIcons(*pUiElement->Rect(UI_ELEM_FRIEND_ICON), &Button, ColorRGBA(0.94f, 0.4f, 0.4f, 1.0f), TextRender()->DefaultTextOutlineColor(), EQmIcon::HEART, FONT_ICON_HEART, TEXTALIGN_MC);

					if(pItem->m_FriendNum > 1)
					{
						str_format(aTemp, sizeof(aTemp), "%d", pItem->m_FriendNum);
						TextRender()->TextColor(0.94f, 0.8f, 0.8f, 1.0f);
						MeasureColumn(Id, aTemp, 9.0f);
						Ui()->DoLabel(&Button, aTemp, 9.0f, TEXTALIGN_MC, QmBrowserTableLabelProperties(Button.w));
						TextRender()->TextColor(TextRender()->DefaultTextColor());
					}
				}
			}
			else if(Id == COL_PLAYERS)
			{
				// 为字形和描边留出余量，避免右对齐的末位数字贴住列裁剪边界。
				Button.VMargin(2.0f, &Button);
				str_format(aTemp, sizeof(aTemp), "%i/%i", pItem->m_NumFilteredPlayers, ServerBrowser()->Max(*pItem));
				if(g_Config.m_BrFilterString[0] && (pItem->m_QuickSearchHit & IServerBrowser::QUICK_PLAYER))
				{
					TextRender()->TextColor(gs_HighlightedTextColor);
				}
				MeasureColumn(Id, aTemp, FontSize, 4.0f);
				Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_PLAYERS), &Button, aTemp, FontSize, TEXTALIGN_MR, QmBrowserTableLabelProperties(Button.w));
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			else if(Id == COL_QM_CLIENTS)
			{
				const int QmClients = pItem->m_QmClientCount;
				if(QmClients > 0)
				{
					Button.VMargin(2.0f, &Button);
					str_format(aTemp, sizeof(aTemp), "%d", QmClients);
					TextRender()->TextColor(gs_QmClientCountColor);
					MeasureColumn(Id, aTemp, FontSize, 4.0f);
					Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_QM_CLIENTS), &Button, aTemp, FontSize, TEXTALIGN_MR, QmBrowserTableLabelProperties(Button.w));
					TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
			}
			else if(Id == COL_PING)
			{
				Button.VMargin(4.0f, &Button);
				CServerInfo::FormatLatency(aTemp, pItem->m_Latency);
				// 占位符 "--"（地区未知/测量超时）保持默认色，不按 999ms 涂红。
				if(g_Config.m_UiColorizePing && CServerInfo::IsUsableLatency(pItem->m_Latency))
				{
					TextRender()->TextColor(GetServerListPingColor(pItem, ServerBrowser()->GetCurrentClientLocation()));
				}
				MeasureColumn(Id, aTemp, FontSize, 8.0f);
				Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_PING), &Button, aTemp, FontSize, TEXTALIGN_MR, QmBrowserTableLabelProperties(Button.w));
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			Ui()->ClipDisable();
		}
		Ui()->ClipDisable();
	}
	// 一次按压只测量一次可见内容，拖动时复用同一组期望宽度，避免列表刷新使边界跳动。
	if(MeasureColumnContent)
		for(int i = 0; i < NumCols; ++i)
			s_aResizeStart[i].m_PreferredWidth = std::max(s_aResizeStart[i].m_MinWidth, aContentWidths[s_aCols[i].m_Id]);
	if(EndVisibleItem < NumServers)
		s_ListBox.SkipItems(NumServers - EndVisibleItem);

	const int NewSelected = s_ListBox.DoEnd();
	const bool ListScrollActive = QmMenuUiScrollPerfActive(s_ListBox.WheelConsumedThisFrame(), s_ListBox.ScrollbarActive(), s_ListBox.ScrollbarAnimating());
	const double ListFrameDurationMs = PerfListFrameEnabled ? std::chrono::duration<double, std::milli>(time_get_nanoseconds() - ListFrameStartTime).count() : -1.0;
	if(PerfListFrameEnabled)
	{
		if(QmPerfShouldLogDuration(ListFrameDurationMs, false))
		{
			char aPayload[256];
			const int ItemsTotal = ServerBrowser()->NumSortedServers();
			const int RowsSkipped = maximum(0, ItemsTotal - RowsRendered);
			str_format(aPayload, sizeof(aPayload),
				"event=list_frame page=server_browser items_total=%d rows_visible=%d rows_rendered=%d rows_iterated=%d rows_skipped=%d dur_ms=%.3f source=server_browser",
				ItemsTotal, RowsVisible, RowsRendered, RowsIterated, RowsSkipped, ListFrameDurationMs);
			QmPerfLogPayload("perf/interaction", aPayload, Client(), "server_browser");
		}
	}
	if(ListScrollActive)
	{
		StartSettingsPerfScrollWindow("server_browser_scroll", SettingsPerfContextName(), "server_browser", "none");
		SQmMenuUiFramePerf MenuUiPerf;
		MenuUiPerf.m_pPage = "server_browser";
		MenuUiPerf.m_pOperation = "server_browser_scroll";
		MenuUiPerf.m_ItemsTotal = NumServers;
		MenuUiPerf.m_ItemsVisible = RowsVisible;
		MenuUiPerf.m_ItemsProcessed = RowsIterated;
		MenuUiPerf.m_ItemsSkipped = maximum(0, NumServers - RowsRendered);
		MenuUiPerf.m_UiMs = (float)ListFrameDurationMs;
		QmLogMenuUiFramePerf(MenuUiPerf, Client());
	}
	if(NumServers * BrowserRowStep > ListView.h)
	{
		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
		CUIRect Fade = ListView;
		Fade.VSplitRight(BrowserScrollbarReserve, &Fade, nullptr);
		constexpr float FadeHeight = 44.0f;
		Fade.y += maximum(Fade.h - FadeHeight, 0.0f);
		Fade.h = minimum(Fade.h, FadeHeight);
		// 渐变的下沿必须跟容器圆角走：它是列表体最底下 44px 的压暗层，方形下沿会正好
		// 把卡片底部的两个圆角盖成直角（用户实机看到的就是这一条）。上沿接在内容中间，
		// 保持直边——那里加圆角会把跨过渐变起点的行切出缺口。
		const float FadeRadius = ui_token::list::ContainerRadius(ListView.w, ListView.h);
		Fade.Draw4(
			BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)),
			BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)),
			BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)),
			BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)),
			IGraphics::CORNER_BL | IGraphics::CORNER_BR, FadeRadius);
	}
	if(NewSelected != m_SelectedIndex)
	{
		m_SelectedIndex = NewSelected;
		if(m_SelectedIndex >= 0)
		{
			// select the new server
			const CServerInfo *pItem = NewSelected >= 0 && NewSelected < NumServers ? ServerBrowser()->SortedGet(vVisibleServerIndices[NewSelected]) : nullptr;
			if(pItem)
			{
				str_copy(g_Config.m_UiServerAddress, pItem->m_aAddress);
				m_ServerBrowserShouldRevealSelection = true;
			}
		}
	}

	WasListboxItemActivated = s_ListBox.WasItemActivated();
}

void CMenus::RenderServerbrowserMapFilterSelector(CUIRect Selector)
{
	if(Selector.w < 150.0f || Selector.h <= 0.0f)
		return;

	static ui_widget::SDiscreteSliderState s_SliderState;
	static int s_SliderId;
	static int s_EmptyToggleId;
	static int s_FavoriteToggleId;
	const IUiContext Context = SettingsUiContext("server_browser_map_filter");

	// 滑条档位直接读写类别掩码：0 不筛选，1-6 依次是两种模式通用的六个难度档位。
	int SliderLevel = QmMapVotes::MapBrowserCategoryLevel(g_Config.m_QmMapBrowserCategoryMask);

	const SQmBrowserMapFilterLayout Layout = QmBrowserMapFilterLayout(Selector, TextRender()->TextWidth(11.0f, Localize("Favorite maps")));
	SLabelProperties HeadingProps;
	HeadingProps.m_MaxWidth = Layout.m_Heading.w;
	HeadingProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&Layout.m_Heading, Localize("Server filter"), 14.0f, TEXTALIGN_MC, HeadingProps);
	CUIRect CurrentLabel = Layout.m_CurrentLabel;
	const CUIRect SliderRect = Layout.m_Slider;

	const bool SliderChanged = ui_widget::DiscreteSlider(Context, &s_SliderId, s_SliderState, &SliderLevel, 0, 6, SliderRect);
	if(SliderChanged)
	{
		g_Config.m_QmMapBrowserCategoryMask = QmMapVotes::MapBrowserCategoryMaskForLevel(SliderLevel);
		Client()->ServerBrowserUpdate();
	}

	// 左侧文本跟随滑块：显示当前档位名；多选或右侧面板勾了专属类别时显示「自定义」。
	char aCurrentLabel[48];
	const int DisplayLevel = SliderLevel;
	if(DisplayLevel < 0 || DisplayLevel > 6)
		str_copy(aCurrentLabel, Localize("Custom"));
	else if(DisplayLevel == 0)
		str_copy(aCurrentLabel, Localize("No filter"));
	else
		str_copy(aCurrentLabel, Localize(QmMapVotes::s_apMapBrowserCategoryKeys[DisplayLevel - 1]));
	if(DisplayLevel >= 0 && DisplayLevel <= 6)
		TextRender()->TextColor(ui_widget::ResolveDiscreteSliderStyle(DisplayLevel, 0, 6).m_Color);
	SLabelProperties CurrentProps;
	CurrentProps.m_MaxWidth = CurrentLabel.w;
	CurrentProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&CurrentLabel, aCurrentLabel, 11.0f, TEXTALIGN_MR, CurrentProps);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	GameClient()->m_Tooltips.DoToolTip(&s_SliderId, &SliderRect, Localize("Difficulty"));

	// 只看空服：与难度档位互相独立，两者可叠加。
	bool EmptyOnly = g_Config.m_QmMapBrowserEmptyOnly != 0;
	{
		char aLabel[32];
		str_format(aLabel, sizeof(aLabel), "0 %s", Localize("Players"));
		SLabelProperties Props;
		Props.m_MaxWidth = Layout.m_EmptyLabel.w;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Layout.m_EmptyLabel, aLabel, 11.0f, TEXTALIGN_MR, Props);
	}
	if(ui_widget::Toggle(Context, &s_EmptyToggleId, &EmptyOnly, Layout.m_EmptyToggle))
	{
		g_Config.m_QmMapBrowserEmptyOnly = EmptyOnly ? 1 : 0;
		Client()->ServerBrowserUpdate();
	}
	GameClient()->m_Tooltips.DoToolTip(&s_EmptyToggleId, &Layout.m_EmptyGroup, Localize("Filter empty servers in browser"));

	// 收藏开关与开关左右并列：右侧「收藏地图」文本说明 + 开关 + 星标，整组右对齐。
	bool FavoriteOnly = g_Config.m_QmMapBrowserFavoriteOnly != 0;
	const CUIRect FavoriteToggle = Layout.m_FavoriteToggle;
	CUIRect FavoriteIcon = Layout.m_FavoriteIcon;
	const CUIRect FavoriteGroup = Layout.m_FavoriteGroup;
	SLabelProperties FavoriteProps;
	FavoriteProps.m_MaxWidth = Layout.m_FavoriteLabel.w;
	FavoriteProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&Layout.m_FavoriteLabel, Localize("Favorite maps"), 11.0f, TEXTALIGN_MR, FavoriteProps);
	if(ui_widget::Toggle(Context, &s_FavoriteToggleId, &FavoriteOnly, FavoriteToggle))
	{
		g_Config.m_QmMapBrowserFavoriteOnly = FavoriteOnly ? 1 : 0;
		Client()->ServerBrowserUpdate();
	}
	FavoriteIcon.VMargin(std::min(1.0f, FavoriteIcon.w * 0.25f), &FavoriteIcon);
	if(g_Config.m_QmUiFavoriteIconFilled != 0)
	{
		// 收藏点亮：开关拨动时描边 ↔ 实心形变（实心样式关着时两端同形，走下方原绘制）。
		// 染色与静止态同源：DrawIconSwapBinding 内部走 ConfiguredQmUiIconColor 的收藏语义色。
		IUiContext FavoriteSwapCtx = ui_widget::ControlContext(Ui());
		ui_widget::SIconSwapBinding FavoriteSwap = ui_widget::ResolveIconSwapBinding(
			FavoriteSwapCtx, EQmIconSwapPair::STAR_FAVORITE,
			EQmIcon::STAR, FontIcons::FONT_ICON_STAR,
			EQmIcon::STAR, FontIcons::FONT_ICON_STAR,
			FavoriteOnly ? 1.0f : 0.0f, str_quickhash("map-filter-favorite-star"), &s_FavoriteToggleId, 0.26f, true);
		ui_widget::DrawIconSwapBinding(FavoriteSwapCtx, FavoriteIcon, TextRender()->GetTextColor(), &FavoriteSwap);
	}
	else
	{
		// 与投票页的收藏开关同源：开启实心、关闭空心，字形由填充作用域决定。
		const CQmFavoriteStarFillScope FavoriteStarFillScope(FavoriteOnly);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		TextRender()->TextColor(FavoriteOnly ? ColorRGBA(1.0f, 0.85f, 0.2f, 1.0f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.62f));
		Ui()->DoLabel_QmIcon(&FavoriteIcon, EQmIcon::STAR, FONT_ICON_STAR, 13.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_FavoriteToggleId, &FavoriteGroup, Localize("Favorite maps"));
}

// 状态栏不再有提示行：用户明确说「服务器列表刷新失败，正在显示上次列表」这一行没必要，
// 所以这里连绘制带占位一起删掉。布局接口（QmBrowserStatusLayout / QmBrowserPanelLayout
// 的 ShowNotice）仍然保留，浏览器两处调用一律传 false：既不画字，也不为它预留高度，
// 不会留下一行空白。IsServerlistError() 的其它用途（空列表态文案、后台加载判定）不受影响。

// 浏览器面板里自绘的三处滚动区（好友列表 / 筛选面板 / 状态栏）现在分两条口径。
// 静态面板（筛选 / 状态栏）走**非覆盖式**：Begin() 会把 thickness 当作占位从 clip
// 里切走，内容矩形天然内缩，行右缘与轨道之间由 margin 留出缝隙，调用点不需要也不
// 允许再手写一次让位减法（双扣）。参数全部由列表 token 派生：可见轨宽 =
// thickness − 2×margin = (SCROLLBAR_WIDTH + 2×SCROLLBAR_MARGIN) − 2×SCROLLBAR_MARGIN
// = SCROLLBAR_WIDTH = 8，内容与轨道之间的缝 = SCROLLBAR_MARGIN = 4。
// 「轨道隐形」是 thickness/margin 取值把可见宽算成 0，与是否覆盖无关。
// 好友列表例外：它的内容高度在展开/折叠动画里逐帧变化、轨道随时可能进出，
// 占位必须恒定，见下面的 ApplyFriendsScrollbar。
static void ApplyBrowserScrollbar(CScrollRegionParams &Params, float TrackInset)
{
	Params.m_ScrollbarThickness = ui_token::list::SCROLLBAR_WIDTH + 2.0f * ui_token::list::SCROLLBAR_MARGIN;
	Params.m_ScrollbarMargin = ui_token::list::SCROLLBAR_MARGIN;
	Params.m_ScrollbarOverlay = false;
	// 非覆盖式不需要显示系数与横向滑出：轨道显隐直接走占位那一档。
	Params.m_ScrollbarRevealSec = 0.0f;
	Params.m_ScrollbarSlide = 0.0f;
	// 宿主按容器圆角给长度方向内缩：滑块端头不会压到（或探出）面板圆角。
	Params.m_ScrollbarTrackInset = std::max(0.0f, TrackInset);
	// 占位不跟着显隐动画：面板里的行宽只在轨道出现时收一档，不会逐帧抖。
	Params.m_AnimateScrollbarReserve = false;
	Params.m_ScrollbarAlwaysReserved = false;
	// FILTER_GRID 的 policy 默认把轨道设成 HIDDEN；这三处要的是可见、能拖的轨道。
	Params.m_HideScrollbar = false;
}

// 好友列表的轨道：与非覆盖式面板同一档（可见宽 8、外边距 4、缝 4），但**开启占位动画**。
// 没有滚动条时占位为 0，卡片与分类表头占满全宽；轨道出现/消失时占位宽度（0↔16）
// 走同一档缓动，内容连续让位而不是整段横跳。好友列表的内容高度是展开进度 ExpandT
// 的函数，逐帧都会跨过溢出阈值，只有占位动画能吸收这个跳变；恒定让位会让没有滚动条
// 时也永久空出一列（实机反馈：正常情况下应当占满全宽）。
static void ApplyFriendsScrollbar(CScrollRegionParams &Params, float TrackInset)
{
	Params.m_ScrollbarThickness = ui_token::list::SCROLLBAR_WIDTH + 2.0f * ui_token::list::SCROLLBAR_MARGIN;
	Params.m_ScrollbarMargin = ui_token::list::SCROLLBAR_MARGIN;
	Params.m_ScrollbarOverlay = false;
	// 非覆盖式不需要显示系数与横向滑出：轨道显隐直接走占位那一档动画。
	Params.m_ScrollbarRevealSec = 0.0f;
	Params.m_ScrollbarSlide = 0.0f;
	Params.m_ScrollbarTrackInset = std::max(0.0f, TrackInset);
	// 占位跟着显隐动画走：出现/消失都平滑，展开动画中途跨过阈值也不会闪一列。
	Params.m_AnimateScrollbarReserve = true;
	Params.m_ScrollbarAlwaysReserved = false;
	Params.m_HideScrollbar = false;
}

void CMenus::RenderServerbrowserStatusBox(CUIRect StatusBox, bool WasListboxItemActivated)
{
	if(StatusBox.w <= 0.0f || StatusBox.h <= 0.0f)
		return;
	static CScrollRegion s_StatusScrollRegion;
	SQmScrollRequest ScrollRequest;
	ScrollRequest.m_Profile = EQmScrollProfile::SETTINGS_INNER;
	ScrollRequest.m_RowExtent = 24.0f;
	ScrollRequest.m_RowsPerStep = 2;
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(QmResolveScrollPolicy(ScrollRequest));
	// 轨道口径与好友列表、筛选面板相同：非覆盖式，可见轨宽 8、内容与轨道之间留 4 的缝。
	ApplyBrowserScrollbar(ScrollParams, ui_token::radius::CARD);
	// 内容矩形只从 Begin() 回写的 clip 取（非覆盖式已把轨道占位切走），调用点不再减法。
	// 首帧测高按「滚动条已占位」的宽度算，与随后 Begin 回写的宽度同宽，避免开面板时先错一帧。
	CUIRect StatusContent = StatusBox;
	StatusContent.w = std::max(0.0f, StatusContent.w - ScrollParams.m_ScrollbarThickness);
	// 提示行已删除：布局按「无提示行」测高，下面也不再画那条文案（不留空行）。
	s_StatusScrollRegion.SetContentHeightForNextFrame(QmBrowserStatusLayout(StatusContent, false).m_ContentHeight);
	vec2 ScrollOffset(0.0f, 0.0f);
	s_StatusScrollRegion.Begin(&StatusBox, &ScrollOffset, &ScrollParams);
	StatusBox.y += ScrollOffset.y;
	const SQmBrowserStatusLayout Layout = QmBrowserStatusLayout(StatusBox, false);
	s_StatusScrollRegion.AddRect({StatusBox.x, StatusBox.y, StatusBox.w, Layout.m_ContentHeight});
	CUIRect RefreshBar = Layout.m_RefreshBar;
	const auto DoFieldLabel = [&](const CUIRect &Rect, const char *pText) {
		SLabelProperties Props;
		Props.m_MaxWidth = Rect.w;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Rect, pText, 14.0f, TEXTALIGN_ML, Props);
	};
	static float s_LoadingProgressionFadeEnd = 0.0f;
	if(ServerBrowser()->IsRefreshing() && ServerBrowser()->LoadingProgression() < 100)
	{
		s_LoadingProgressionFadeEnd = Client()->GlobalTime() + 2.0f;
	}
	const float LoadingProgressionTimeDiff = s_LoadingProgressionFadeEnd - Client()->GlobalTime();
	if(LoadingProgressionTimeDiff > 0.0f)
	{
		const float RefreshBarAlpha = minimum(LoadingProgressionTimeDiff, 0.8f);
		RefreshBar.h = 2.0f;
		RefreshBar.w *= ServerBrowser()->LoadingProgression() / 100.0f;
		RefreshBar.Draw(BrowserOpacityColor(ColorRGBA(1.0f, 1.0f, 1.0f, RefreshBarAlpha)), IGraphics::CORNER_NONE, 0.0f);
	}

	const CUIRect QuickSearch = Layout.m_SearchInput;
	const CUIRect QuickExclude = Layout.m_ExcludeInput;

	// render quick search
	{
		char aBufSearch[64];
		str_format(aBufSearch, sizeof(aBufSearch), "%s:", Localize("Search"));
		DoFieldLabel(Layout.m_SearchLabel, aBufSearch);

		static CLineInput s_FilterInput(g_Config.m_BrFilterString, sizeof(g_Config.m_BrFilterString));
		static char s_aTooltipText[64];
		str_format(s_aTooltipText, sizeof(s_aTooltipText), "%s: \"solo; nameless tee; kobra 2\"", Localize("Example of usage"));
		GameClient()->m_Tooltips.DoToolTip(&s_FilterInput, &QuickSearch, s_aTooltipText);
		if(!Ui()->IsPopupOpen() && Input()->KeyPress(KEY_F) && Input()->ModifierIsPressed())
		{
			Ui()->SetActiveItem(&s_FilterInput);
			s_FilterInput.SelectAll();
		}
		if(Ui()->ActiveItem() == &s_FilterInput)
			s_StatusScrollRegion.AddRect(QuickSearch, true);
		const IUiContext ServerBrowserSearchCtx = SettingsUiContext("server_browser_search");
		ui_widget::SInputFieldOptions SearchOptions;
		SearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
		SearchOptions.m_Clearable = true;
		SearchOptions.m_FontSize = 12.0f;
		if(ui_widget::InputField(ServerBrowserSearchCtx, &s_FilterInput, QuickSearch, SearchOptions).m_Changed)
			Client()->ServerBrowserUpdate();
	}

	// render quick exclude
	{
		// 标签不再带图标（与「搜索:」同行对齐）：排除语义由图标的输入框自身表达，
		// 输入框内用 BAN 图标并把占位符写成「排除」。
		char aBufExclude[64];
		str_format(aBufExclude, sizeof(aBufExclude), "%s:", Localize("Exclude"));
		DoFieldLabel(Layout.m_ExcludeLabel, aBufExclude);

		static CLineInput s_ExcludeInput(g_Config.m_BrExcludeString, sizeof(g_Config.m_BrExcludeString));
		static char s_aTooltipText[64];
		str_format(s_aTooltipText, sizeof(s_aTooltipText), "%s: \"CHN; [A]\"", Localize("Example of usage"));
		GameClient()->m_Tooltips.DoToolTip(&s_ExcludeInput, &QuickExclude, s_aTooltipText);
		if(!Ui()->IsPopupOpen() && Input()->KeyPress(KEY_X) && Input()->ShiftIsPressed() && Input()->ModifierIsPressed())
		{
			Ui()->SetActiveItem(&s_ExcludeInput);
			s_ExcludeInput.SelectAll();
		}
		if(Ui()->ActiveItem() == &s_ExcludeInput)
			s_StatusScrollRegion.AddRect(QuickExclude, true);
		const IUiContext ServerBrowserExcludeCtx = SettingsUiContext("server_browser_exclude");
		ui_widget::SInputFieldOptions ExcludeOptions;
		ExcludeOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
		ExcludeOptions.m_Clearable = true;
		ExcludeOptions.m_FontSize = 12.0f;
		ExcludeOptions.m_pPlaceholder = Localize("Exclude");
		ExcludeOptions.m_pLeadingIcon = FONT_ICON_BAN;
		ExcludeOptions.m_LeadingQmIcon = static_cast<int>(EQmIcon::BAN);
		if(ui_widget::InputField(ServerBrowserExcludeCtx, &s_ExcludeInput, QuickExclude, ExcludeOptions).m_Changed)
			Client()->ServerBrowserUpdate();
	}

	if(Layout.m_MapFilter.w > 0.0f)
		RenderServerbrowserMapFilterSelector(Layout.m_MapFilter);

	// render status
	{
		const CUIRect ServersOnline = Layout.m_Servers;
		const CUIRect PlayersOnline = Layout.m_Players;
		SLabelProperties Props;
		Props.m_MaxWidth = ServersOnline.w;
		Props.m_EllipsisAtEnd = true;

		char aBuf[128];
		if(ServerBrowser()->NumServers() != 1)
			str_format(aBuf, sizeof(aBuf), Localize("%d of %d servers"), ServerBrowser()->NumSortedServers(), ServerBrowser()->NumServers());
		else
			str_format(aBuf, sizeof(aBuf), Localize("%d of %d server"), ServerBrowser()->NumSortedServers(), ServerBrowser()->NumServers());
		Ui()->DoLabel(&ServersOnline, aBuf, 12.0f, TEXTALIGN_MR, Props);

		if(ServerBrowser()->NumSortedPlayers() != 1)
			str_format(aBuf, sizeof(aBuf), Localize("%d players"), ServerBrowser()->NumSortedPlayers());
		else
			str_format(aBuf, sizeof(aBuf), Localize("%d player"), ServerBrowser()->NumSortedPlayers());
		Ui()->DoLabel(&PlayersOnline, aBuf, 12.0f, TEXTALIGN_MR, Props);
	}

	// address info
	{
		const CUIRect ServerAddrEditBox = Layout.m_AddressInput;
		DoFieldLabel(Layout.m_AddressLabel, Localize("Server address:"));
		static CLineInput s_ServerAddressInput(g_Config.m_UiServerAddress, sizeof(g_Config.m_UiServerAddress));
		if(Ui()->ActiveItem() == &s_ServerAddressInput)
			s_StatusScrollRegion.AddRect(ServerAddrEditBox, true);
		const IUiContext ServerBrowserAddressCtx = SettingsUiContext("server_browser_address");
		ui_widget::SInputFieldOptions AddressOptions;
		AddressOptions.m_Clearable = true;
		AddressOptions.m_FontSize = 12.0f;
		if(ui_widget::InputField(ServerBrowserAddressCtx, &s_ServerAddressInput, ServerAddrEditBox, AddressOptions).m_Changed)
			m_ServerBrowserShouldRevealSelection = true;
	}

	// buttons
	{
		const CUIRect ButtonRefresh = Layout.m_RefreshButton;
		const CUIRect ButtonConnect = Layout.m_ConnectButton;

		// refresh button
		{
			char aLabelBuf[32] = {0};
			const auto &&RefreshLabelFunc = [this, aLabelBuf]() mutable {
				if(ServerBrowser()->IsRefreshing() || ServerBrowser()->IsGettingServerlist())
					str_format(aLabelBuf, sizeof(aLabelBuf), "%s%s", FONT_ICON_ARROW_ROTATE_RIGHT, FONT_ICON_ELLIPSIS);
				else
					str_copy(aLabelBuf, FONT_ICON_ARROW_ROTATE_RIGHT);
				return aLabelBuf;
			};

			SMenuButtonProperties Props;
			Props.m_HintRequiresStringCheck = true;
			Props.m_HintCanChangePositionOrSize = true;
			Props.m_UseIconFont = true;
			// 工具条按钮是长条控件：端头直接给胶囊档，绘制层按短边一半裁，
			// 与列表胶囊行/输入框外壳同一族圆角。
			Props.m_Rounding = ui_token::radius::PILL;

			static CButtonContainer s_RefreshButton;
			if(Ui()->DoButton_Menu(m_RefreshButton, &s_RefreshButton, RefreshLabelFunc, &ButtonRefresh, Props) || (!Ui()->IsPopupOpen() && (Input()->KeyPress(KEY_F5) || (Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed()))))
			{
				RefreshBrowserTab(true);
			}
		}

		// connect button
		{
			const auto &&ConnectLabelFunc = []() { return FONT_ICON_RIGHT_TO_BRACKET; };

			SMenuButtonProperties Props;
			Props.m_UseIconFont = true;
			Props.m_HintCanChangePositionOrSize = true;
			// 与刷新按钮同一条工具条，圆角同档：长条按钮按胶囊绘制。
			Props.m_Rounding = ui_token::radius::PILL;
			// 连接动作保留绿色语义，颜色改由统一语义色板提供（原为裸字面量 0.5/1.0/0.5）。
			Props.m_Color = ui_token::color::SUCCESS.WithAlpha(0.5f);

			static CButtonContainer s_ConnectButton;
			if(Ui()->DoButton_Menu(m_ConnectButton, &s_ConnectButton, ConnectLabelFunc, &ButtonConnect, Props) || WasListboxItemActivated || (!Ui()->IsPopupOpen() && Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER)))
			{
				Connect(g_Config.m_UiServerAddress);
			}
		}
	}
	s_StatusScrollRegion.End();
}

void CMenus::Connect(const char *pAddress, EConnectIntent Intent)
{
	if(Intent == EConnectIntent::Manual)
		StopFriendAutoFollow(m_FriendAutoFollowState);
	if(Client()->State() == IClient::STATE_ONLINE && GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmDisconnectTime && g_Config.m_ClConfirmDisconnectTime >= 0)
	{
		str_copy(m_aNextServer, pAddress);
		// 连别的服务器会断开当前对局：Cancel/OK + 危险色；确认后由浏览器页轮询去连。
		s_SwitchServerConfirm.Reset();
		s_SwitchServerConfirm.DefaultButtons();
		s_SwitchServerConfirm.m_Tone = CUi::SConfirmPopupContext::ETone::DANGER;
		str_copy(s_SwitchServerConfirm.m_aMessage, Localize("Are you sure that you want to disconnect and switch to a different server?"), sizeof(s_SwitchServerConfirm.m_aMessage));
		Ui()->ShowPopupConfirm(Ui()->MouseX(), Ui()->MouseY(), &s_SwitchServerConfirm);
		s_SwitchServerPending = true;
	}
	else
	{
		Client()->Connect(pAddress);
	}
}

void CMenus::PopupConfirmSwitchServer()
{
	Client()->Connect(m_aNextServer);
}

void CMenus::RenderServerbrowserFilters(CUIRect View)
{
	const float RowHeight = 18.0f;
	View.Margin(5.0f, &View);

	CUIRect Button, ResetButton;
	View.HSplitBottom(RowHeight, &View, &ResetButton);
	View.HSplitBottom(3.0f, &View, nullptr);

	// 长译文或较矮窗口通过整体滚动保留全部筛选项，重置按钮固定在底部。
	static CScrollRegion s_FilterScrollRegion;
	SQmScrollRequest ScrollRequest;
	ScrollRequest.m_Profile = EQmScrollProfile::SETTINGS_GRID;
	ScrollRequest.m_RowExtent = RowHeight;
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(QmResolveScrollPolicy(ScrollRequest));
	// 工具箱三个页签共用同一档滚动条：非覆盖式，可见轨宽 8，
	// 内容与轨道之间由 margin 天然留出 4 的缝，与服务器列表、好友列表同一口径。
	ApplyBrowserScrollbar(ScrollParams, ui_token::radius::CARD);
	vec2 ScrollOffset(0.0f, 0.0f);
	s_FilterScrollRegion.Begin(&View, &ScrollOffset, &ScrollParams);
	View.y += ScrollOffset.y;
	const float ContentTop = View.y;
	// 表单宽度直接取 Begin() 回写的 clip：轨道占位已经切走一档，这里再减就是双扣。
	RenderServerbrowserFilterForm(View);

	// countries and types filters
	if(ServerBrowser()->CommunityCache().CountriesTypesFilterAvailable())
	{
		const ColorRGBA ColorActive = BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f));
		const ColorRGBA ColorInactive = BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f));
		// 页签的**选中态**只表达「当前在哪一页」，用主题选中色（强调色派生），与列表行/格、
		// 下拉弹层条目同一份；内容区背板仍用上面那档暗色（它表达的是容器，不是选中）。
		const ColorRGBA TabSelectedColor = Ui()->QmControlTheme().m_Selected;

		CUIRect TabContents, CountriesTab, TypesTab;
		View.HSplitTop(6.0f, nullptr, &View);
		View.HSplitTop(19.0f, &Button, &View);
		// 页签内容区按国家/类型里更高的一档取整数行：末行完整可见，不被裁半行。
		View.HSplitTop(FilterGridExtent(COUNTRY_FILTER_VISIBLE_ROWS, COUNTRY_FILTER_ROW_HEIGHT), &TabContents, &View);
		Button.VSplitMid(&CountriesTab, &TypesTab);
		// 与社区卡片同属工具箱面板内的顶层卡片，圆角跟随面板的 radius::CARD，
		// 否则外缘大圆角里会套一圈更方的小圆角。
		TabContents.Draw(ColorActive, IGraphics::CORNER_B, ui_token::radius::CARD);

		enum EFilterTab
		{
			FILTERTAB_COUNTRIES = 0,
			FILTERTAB_TYPES,
		};
		static EFilterTab s_ActiveTab = FILTERTAB_COUNTRIES;
		static EFilterTab s_PrevFilterTab = FILTERTAB_COUNTRIES;
		static float s_FilterTabDirection = 0.0f;
		const CUIRect aFilterTabSlots[] = {CountriesTab, TypesTab};
		const int Pressed = ui_widget::CapsuleTabBarPressedIndex(TabBarUiContext(), aFilterTabSlots, std::size(aFilterTabSlots), 0.0f);
		if(Pressed >= 0)
			s_ActiveTab = static_cast<EFilterTab>(Pressed);

		static CButtonContainer s_CountriesButton;
		DoButton_MenuTab(&s_CountriesButton, Localize("Countries"), s_ActiveTab == FILTERTAB_COUNTRIES, &CountriesTab, IGraphics::CORNER_TL, nullptr, &ColorInactive, &TabSelectedColor, nullptr, ui_token::radius::MENU);

		static CButtonContainer s_TypesButton;
		DoButton_MenuTab(&s_TypesButton, Localize("Types"), s_ActiveTab == FILTERTAB_TYPES, &TypesTab, IGraphics::CORNER_TR, nullptr, &ColorInactive, &TabSelectedColor, nullptr, ui_token::radius::MENU);

		if(s_ActiveTab != s_PrevFilterTab)
		{
			s_FilterTabDirection = ResolveUiSwitchDirection(GameClient()->UiRuntimeV2()->AnimRuntime(), UiAnimNodeKey("browser_filter_tab_switch"), s_FilterTabDirection, s_ActiveTab > s_PrevFilterTab ? 1.0f : -1.0f);
			TriggerUiSwitchAnimation(UiAnimNodeKey("browser_filter_tab_switch"), ui_token::motion::TAB_SWITCH.m_DurationSec);
			s_PrevFilterTab = s_ActiveTab;
		}
		const float TransitionStrength = ReadUiSwitchAnimation(UiAnimNodeKey("browser_filter_tab_switch"));
		const bool TransitionActive = TransitionStrength > 0.0f && s_FilterTabDirection != 0.0f;
		const float TransitionAlpha = UiSwitchAnimationAlpha(TransitionStrength);
		CUIRect AnimatedTabContents = TabContents;
		if(TransitionActive)
		{
			Ui()->ClipEnable(&TabContents);
			ApplyUiSwitchOffset(AnimatedTabContents, TransitionStrength, s_FilterTabDirection, false, 0.08f, 24.0f, 120.0f);
		}

		if(s_ActiveTab == FILTERTAB_COUNTRIES)
		{
			RenderServerbrowserCountriesFilter(AnimatedTabContents);
		}
		else if(s_ActiveTab == FILTERTAB_TYPES)
		{
			RenderServerbrowserTypesFilter(AnimatedTabContents);
		}

		if(TransitionActive)
		{
			if(TransitionAlpha > 0.0f)
				DrawUiSwitchTransitionOverlay(TabContents, BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha)));
			Ui()->ClipDisable();
		}
	}

	s_FilterScrollRegion.AddRect({View.x, ContentTop, View.w, std::max(0.0f, View.y - ContentTop)});
	s_FilterScrollRegion.End();

	// 重置按钮和其他筛选按钮用同一套胶囊：同一个面板里不混两种按钮形态。
	static unsigned char s_ResetFilterChipId = 0;
	CUIRect ResetChip = ResetButton;
	// 与表单内容同宽（View.w 仍是 Begin() 回写的 clip 宽）：右缘和 chip / 输入框右缘
	// 对齐，不伸到轨道下面，也不在轨道消失时留下一段空边。
	ResetChip.w = View.w;
	// 与筛选 chip 同高：都是从正文档派生的标准控件高度。
	ResetChip.h = ResolveSettingsControlHeight(ResolveSettingsContentMetrics(View.w).m_BodySize);
	ResetChip.y += std::max(0.0f, (ResetButton.h - ResetChip.h) / 2.0f);
	if(ui_widget::FilterChip(SettingsUiContext("server_browser_filter_reset"), &s_ResetFilterChipId, ResetChip, Localize("Reset filter"), false))
	{
		ResetServerbrowserFilters();
	}
}

void CMenus::ResetServerbrowserFilters()
{
	g_Config.m_BrFilterString[0] = '\0';
	g_Config.m_BrExcludeString[0] = '\0';
	g_Config.m_BrFilterFull = 0;
	g_Config.m_BrFilterEmpty = 0;
	g_Config.m_BrFilterSpectators = 0;
	g_Config.m_BrFilterFriends = 0;
	g_Config.m_BrFilterCountry = 0;
	g_Config.m_BrFilterCountryIndex = -1;
	g_Config.m_BrFilterPw = 0;
	g_Config.m_BrFilterGametype[0] = '\0';
	g_Config.m_BrFilterGametypeStrict = 0;
	g_Config.m_BrFilterConnectingPlayers = 1;
	g_Config.m_BrFilterServerAddress[0] = '\0';
	g_Config.m_BrFilterLogin = false; // TClient
	g_Config.m_QmMapBrowserEmptyOnly = 0;
	g_Config.m_QmMapBrowserFavoriteOnly = 0;
	g_Config.m_QmMapBrowserStarMask = 0;
	g_Config.m_QmMapBrowserCategoryMask = 0;

	if(g_Config.m_UiPage != PAGE_LAN)
	{
		if(ServerBrowser()->CommunityCache().AnyRanksAvailable())
		{
			g_Config.m_BrFilterUnfinishedMap = 0;
		}
		if(g_Config.m_UiPage == PAGE_INTERNET || g_Config.m_UiPage == PAGE_FAVORITES)
		{
			ServerBrowser()->CommunitiesFilter().Clear();
		}
		ServerBrowser()->CountriesFilter().Clear();
		ServerBrowser()->TypesFilter().Clear();
		UpdateCommunityCache(true);
	}

	Client()->ServerBrowserUpdate();
}

void CMenus::RenderServerbrowserDDNetFilter(CUIRect View,
	IFilterList &Filter,
	float RowHeight, int MaxItems, int ItemsPerRow,
	CScrollRegion &ScrollRegion, std::vector<unsigned char> &vItemIds,
	bool UpdateCommunityCacheOnChange,
	const std::function<const char *(int ItemIndex)> &GetItemName,
	const std::function<void(int ItemIndex, CUIRect Item, const void *pItemId, bool Active)> &RenderItem)
{
	vItemIds.resize(MaxItems);

	// 胶囊格：行高/行距/圆角/左右内退全部走列表 token，与设置页胶囊行同一套。
	// 悬浮/选中是整格胶囊（含格间半档行距），文字与图标排在内退后的内容区上。
	const float RowSpacing = ui_token::list::ROW_SPACING;
	const float RowStep = RowHeight + RowSpacing;
	const float CellRadius = ui_token::list::RowRadius(RowHeight);
	// 行内左右内退：单列整行走 token 全量内退（文字不再贴胶囊左缘）；多列网格的格子
	// 只有几十像素宽，全量内退会把旗标/文字挤小，按更小的一档收敛，格子边界仍由胶囊给出。
	const float TokenRowInsetX = ui_token::list::RowInsetX(RowHeight * CUi::ms_FontmodHeight);
	const float ContentInsetX = ItemsPerRow > 1 ? std::min(TokenRowInsetX, ui_token::spacing::XS) : TokenRowInsetX;
	// 容器内边距：与 CListBox 同一条 token（按行高分档，网格不大所以落在 XS/SM）。
	// 四边都内缩——首/末列与首/末行的胶囊不再压在容器圆角上，格子与容器边缘之间
	// 有可见留白（实机反馈：上下左右边距都得做，但不必太大）。
	const float Padding = ui_token::list::ContainerPadding(RowHeight);
	// 只按整数行开口：滚动视口边界落在行与行的缝隙里，首/末行不会被裁成半个胶囊。
	const int VisibleGridRows = maximum(1, (int)std::floor((std::max(0.0f, View.h - 2.0f * Padding) - RowHeight - RowSpacing * 0.5f) / RowStep) + 1);
	// 容器总高 = 整数行内容 + 上下两档内边距（调用点测高用 FilterGridExtent 同一条公式）。
	View.h = std::min(View.h, VisibleGridRows * RowStep + 2.0f * Padding);
	// 行区排在内边距内侧：滚动区、格宽、左右内退都从这条线开始算。
	CUIRect Layout = View;
	Layout.x += Padding;
	Layout.w = maximum(0.0f, Layout.w - 2.0f * Padding);
	Layout.y += Padding;
	Layout.h = maximum(0.0f, Layout.h - 2.0f * Padding);

	vec2 ScrollOffset(0.0f, 0.0f);
	SQmScrollRequest ScrollRequest;
	ScrollRequest.m_Profile = EQmScrollProfile::FILTER_GRID;
	ScrollRequest.m_RowExtent = RowStep;
	ScrollRequest.m_RowsPerStep = 2;
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest);
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	// 这三张胶囊网格（社区 / 国家 / 模式）固定可见行数、内容本来就放得下：不画轨道
	// （回到 FILTER_GRID policy 的 HIDDEN 默认），也不为轨道让位。上一轮为了修
	// 「轨道隐形」把它们强制改成常显轨道，结果最右一列格子反被轨道压住。
	// 滚轮滚动能力保留；宽度直接取外层面板的内容宽（已扣面板轨道让位），
	// 所以格子右缘不会伸到面板轨道下面。
	ScrollParams.m_HideScrollbar = true;
	ScrollRegion.Begin(&Layout, &ScrollOffset, &ScrollParams);
	Layout.y += ScrollOffset.y;

	CUIRect Row;
	int ColumnIndex = 0;
	// 依次淡入 + 上浮：与服务器列表同一套入场语言。三个过滤列表（社区/国家/
	// 类型）共享本函数的静态生命周期，同帧顺序驱动等效于一条时间线；页面
	// 切换时由重启序号统一重播。
	static CQmListEntryAnimation s_EntryAnimation;
	static int s_LastRestartSeq = -1;
	if(s_LastRestartSeq != m_BrowserListEntryRestartSeq)
	{
		s_EntryAnimation.Reset();
		s_LastRestartSeq = m_BrowserListEntryRestartSeq;
	}
	const int64_t EntryNow = time_get();
	const int64_t EntryFreq = maximum<int64_t>(1, time_freq());
	const bool EntryEnabled = g_Config.m_QmUiMotionLevel > 0;
	const float EntryElapsed = s_EntryAnimation.Resolve(EntryNow, EntryFreq, EntryEnabled, g_Config.m_QmUiMotionLevel,
		Ui()->MeasurementOnly() ? EQmListEntryRenderPass::MEASUREMENT : EQmListEntryRenderPass::INTERACTIVE);
	for(int ItemIndex = 0; ItemIndex < MaxItems; ++ItemIndex)
	{
		const int RowIndex = ItemIndex / ItemsPerRow;
		CUIRect Cell;
		if(ColumnIndex == 0)
			Layout.HSplitTop(RowStep, &Row, &Layout);
		Cell = {Row.x + ColumnIndex * (Row.w / ItemsPerRow), Row.y + RowSpacing * 0.5f, Row.w / ItemsPerRow, RowHeight};
		ColumnIndex = (ColumnIndex + 1) % ItemsPerRow;
		if(!ScrollRegion.AddRect(Cell))
			continue;

		const SQmListEntryVisual EntryVisual = QmListEntryVisual(EntryElapsed, RowIndex, g_Config.m_QmUiMotionLevel);
		std::optional<CUiScopedControlAlphaScale> EntryFade;
		if(EntryVisual.m_Fade < 0.999f)
			EntryFade.emplace(Ui(), EntryVisual.m_Fade);
		Cell.y += EntryVisual.m_OffsetY;

		const void *pItemId = &vItemIds[ItemIndex];
		const char *pName = GetItemName(ItemIndex);
		const bool Active = !Filter.Filtered(pName);
		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());

		const int Click = Ui()->DoButtonLogic(pItemId, 0, &Cell, BUTTONFLAG_ALL);
		if(Click == 1 || Click == 2)
		{
			// left/right click to toggle filter
			if(Filter.Empty())
			{
				if(Click == 1)
				{
					// Left click: when all are active, only activate one and none
					for(int j = 0; j < MaxItems; ++j)
					{
						if(const char *pItemName = GetItemName(j);
							j != ItemIndex &&
							!((&Filter == &ServerBrowser()->CountriesFilter() && str_comp(pItemName, IServerBrowser::COMMUNITY_COUNTRY_NONE) == 0) ||
								(&Filter == &ServerBrowser()->TypesFilter() && str_comp(pItemName, IServerBrowser::COMMUNITY_TYPE_NONE) == 0)))
							Filter.Add(pItemName);
					}
				}
				else if(Click == 2)
				{
					// Right click: when all are active, only deactivate one
					if(MaxItems >= 2)
					{
						Filter.Add(GetItemName(ItemIndex));
					}
				}
			}
			else
			{
				bool AllFilteredExceptUs = true;
				for(int j = 0; j < MaxItems; ++j)
				{
					if(const char *pItemName = GetItemName(j);
						j != ItemIndex && !Filter.Filtered(pItemName) &&
						!((&Filter == &ServerBrowser()->CountriesFilter() && str_comp(pItemName, IServerBrowser::COMMUNITY_COUNTRY_NONE) == 0) ||
							(&Filter == &ServerBrowser()->TypesFilter() && str_comp(pItemName, IServerBrowser::COMMUNITY_TYPE_NONE) == 0)))
					{
						AllFilteredExceptUs = false;
						break;
					}
				}
				// When last one is removed, re-enable all currently selectable items.
				// Don't use Clear, to avoid enabling also currently unselectable items.
				if(AllFilteredExceptUs && Active)
				{
					for(int j = 0; j < MaxItems; ++j)
					{
						Filter.Remove(GetItemName(j));
					}
				}
				else if(Active)
				{
					Filter.Add(pName);
				}
				else
				{
					Filter.Remove(pName);
				}
			}

			Client()->ServerBrowserUpdate();
			if(UpdateCommunityCacheOnChange)
				UpdateCommunityCache(true);
		}
		else if(Click == 3)
		{
			// middle click to reset (re-enable all currently selectable items)
			for(int j = 0; j < MaxItems; ++j)
			{
				Filter.Remove(GetItemName(j));
			}
			Client()->ServerBrowserUpdate();
			if(UpdateCommunityCacheOnChange)
				UpdateCommunityCache(true);
		}

		// 内容区左右内退由列表 token 派生：图标、旗标、文字都排在这里，
		// 不再靠各列表自己写 +2/-2 像素补偿；悬浮胶囊仍按整格绘制。
		CUIRect Content = Cell;
		Content.VMargin(std::min(ContentInsetX, std::max(0.0f, Cell.w * 0.5f)), &Content);
		if(Ui()->HotItem() == pItemId && !ScrollRegion.Animating())
		{
			// 悬浮是整格胶囊，只填充不描边：白色描边只属于卡片内的小控件，列表行/格不用。
			// 填充取主题选中色（与 CListBox 的选中/悬浮同一份 m_Selected，alpha 0.22），
			// 不掺白色 token、也不乘任何系数；更不能过 BrowserOpacityColor——那是背板
			// 与描边的表面染色，会把 alpha 再乘一档 qm_ui_opacity（默认 12%），
			// 悬浮因此几乎看不出来。这里只叠入场淡入。
			const ColorRGBA HoverFill = Ui()->QmControlTheme().m_Selected;
			DrawRoundedSurface(Ui(), Cell, HoverFill.WithAlpha(HoverFill.a * Ui()->ControlAlphaScale()), ColorRGBA(), CellRadius);
		}
		RenderItem(ItemIndex, Content, pItemId, Active);
	}

	ScrollRegion.End();
}

void CMenus::RenderServerbrowserCommunitiesFilter(CUIRect View)
{
	CUIRect Tab;
	View.HSplitTop(19.0f, &Tab, &View);
	// 社区卡片是工具箱面板内部的顶层卡片，圆角必须与面板自身（radius::CARD）
	// 一致，否则外缘大圆角里会套一圈明显更方的小圆角。
	Tab.Draw(BrowserOpacityColor(ui_token::color::SURFACE_SUBHEADER.WithAlpha(0.3f)), IGraphics::CORNER_T, ui_token::radius::CARD);
	Ui()->DoLabel(&Tab, Localize("Communities"), 12.0f, TEXTALIGN_MC);
	View.Draw(BrowserOpacityColor(ui_token::color::SURFACE_SUBHEADER), IGraphics::CORNER_B, ui_token::radius::CARD);

	const int MaxEntries = ServerBrowser()->Communities().size();
	if(MaxEntries == 0)
	{
		CUIRect ErrorLabel;
		View.Margin(5.0f, &ErrorLabel);
		SLabelProperties ErrorLabelProps;
		ErrorLabelProps.m_MaxWidth = ErrorLabel.w;
		ErrorLabelProps.SetColor(ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
		Ui()->DoLabel(&ErrorLabel, Localize("Error loading communities"), 10.0f, TEXTALIGN_MC, ErrorLabelProps);
		return;
	}

	const int EntriesPerRow = 1;

	static CScrollRegion s_ScrollRegion;
	static std::vector<unsigned char> s_vItemIds;
	static std::vector<unsigned char> s_vFavoriteButtonIds;

	const float RowHeight = COMMUNITY_FILTER_ROW_HEIGHT;

	const auto &&GetItemName = [&](int ItemIndex) {
		return ServerBrowser()->Communities()[ItemIndex].Id();
	};
	const auto &&RenderItem = [&](int ItemIndex, CUIRect Item, const void *pItemId, bool Active) {
		const auto &Community = ServerBrowser()->Communities()[ItemIndex];
		const float Alpha = ((Active ? 0.9f : 0.2f) + (Ui()->HotItem() == pItemId ? 0.1f : 0.0f)) * Ui()->ControlAlphaScale();

		// 内容区已含胶囊行的左右内退，这里只排图标、名称、人数与收藏星：
		// 星标右缘、人数与图标都跟着内容区走，不再手写 +2/-1 像素补偿。
		CUIRect Icon, NameLabel, PlayerCountIcon, PlayerCountLabel, FavoriteButton;
		Item.VSplitRight(Item.h, &Item, &FavoriteButton);
		Item.VSplitLeft(Item.h * 2.0f, &Icon, &NameLabel);
		NameLabel.VSplitLeft(ui_token::control::IconTextGap(NameLabel.h), nullptr, &NameLabel);
		NameLabel.VSplitRight(8.0f, &NameLabel, &PlayerCountIcon);
		NameLabel.VSplitRight(25.0f, &NameLabel, &PlayerCountLabel);

		const char *pItemName = Community.Id();
		const CCommunityIcon *pIcon = m_CommunityIcons.Find(pItemName);
		if(pIcon != nullptr)
		{
			m_CommunityIcons.Render(pIcon, Icon, Active, Ui()->ControlAlphaScale());
		}

		TextRender()->TextColor(1.0f, 1.0f, 1.0f, Alpha);
		Ui()->DoLabel(&NameLabel, Community.Name(), NameLabel.h * CUi::ms_FontmodHeight, TEXTALIGN_ML);
		char aNumPlayersLabel[8];
		str_format(aNumPlayersLabel, sizeof(aNumPlayersLabel), "%d", Community.NumPlayers());
		Ui()->DoLabel(&PlayerCountLabel, aNumPlayersLabel, 7.0f, TEXTALIGN_MR);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		Ui()->DoLabel_QmIcon(&PlayerCountIcon, EQmIcon::USER, FONT_ICON_USER, 7.0f, TEXTALIGN_MC);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		const bool Favorite = ServerBrowser()->FavoriteCommunitiesFilter().Filtered(pItemName);
		if(DoButton_Favorite(&s_vFavoriteButtonIds[ItemIndex], pItemId, Favorite, &FavoriteButton))
		{
			if(Favorite)
			{
				ServerBrowser()->FavoriteCommunitiesFilter().Remove(pItemName);
			}
			else
			{
				ServerBrowser()->FavoriteCommunitiesFilter().Add(pItemName);
			}
		}
		GameClient()->m_Tooltips.DoToolTip(&s_vFavoriteButtonIds[ItemIndex], &FavoriteButton,
			Favorite ? Localize("Click to remove this community from favorites") : Localize("Click to add this community to favorites"));
	};

	s_vFavoriteButtonIds.resize(MaxEntries);
	RenderServerbrowserDDNetFilter(View, ServerBrowser()->CommunitiesFilter(), RowHeight, MaxEntries, EntriesPerRow, s_ScrollRegion, s_vItemIds, true, GetItemName, RenderItem);
}

void CMenus::RenderServerbrowserCountriesFilter(CUIRect View)
{
	const int MaxEntries = ServerBrowser()->CommunityCache().SelectableCountries().size();
	const int EntriesPerRow = MaxEntries > 8 ? 5 : 4;

	static CScrollRegion s_ScrollRegion;
	static std::vector<unsigned char> s_vItemIds;

	const float RowHeight = COUNTRY_FILTER_ROW_HEIGHT;

	const auto &&GetItemName = [&](int ItemIndex) {
		return ServerBrowser()->CommunityCache().SelectableCountries()[ItemIndex]->Name();
	};
	const auto &&RenderItem = [&](int ItemIndex, CUIRect Item, const void *pItemId, bool Active) {
		// 旗帜保持 2:1，在内容区里居中；内容区由胶囊行的左右内退派生，
		// 内退吃掉宽度时旗帜等比缩小而不是压扁。
		const float FlagWidth = std::min(Item.w, Item.h * 2.0f);
		const float FlagHeight = std::min(Item.h, FlagWidth * 0.5f);
		CUIRect FlagRect{Item.x + (Item.w - FlagWidth) * 0.5f, Item.y + (Item.h - FlagHeight) * 0.5f, FlagWidth, FlagHeight};
		GameClient()->m_CountryFlags.Render(ServerBrowser()->CommunityCache().SelectableCountries()[ItemIndex]->FlagId(), ColorRGBA(1.0f, 1.0f, 1.0f, ((Active ? 0.9f : 0.2f) + (Ui()->HotItem() == pItemId ? 0.1f : 0.0f)) * Ui()->ControlAlphaScale()), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);
	};

	RenderServerbrowserDDNetFilter(View, ServerBrowser()->CountriesFilter(), RowHeight, MaxEntries, EntriesPerRow, s_ScrollRegion, s_vItemIds, false, GetItemName, RenderItem);
}

void CMenus::RenderServerbrowserTypesFilter(CUIRect View)
{
	const int MaxEntries = ServerBrowser()->CommunityCache().SelectableTypes().size();
	const int EntriesPerRow = 3;

	static CScrollRegion s_ScrollRegion;
	static std::vector<unsigned char> s_vItemIds;

	const float RowHeight = TYPE_FILTER_ROW_HEIGHT;

	const auto &&GetItemName = [&](int ItemIndex) {
		return ServerBrowser()->CommunityCache().SelectableTypes()[ItemIndex]->Name();
	};
	const auto &&RenderItem = [&](int ItemIndex, CUIRect Item, const void *pItemId, bool Active) {
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, ((Active ? 0.9f : 0.2f) + (Ui()->HotItem() == pItemId ? 0.1f : 0.0f)) * Ui()->ControlAlphaScale());
		Ui()->DoLabel(&Item, GetItemName(ItemIndex), Item.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	};

	RenderServerbrowserDDNetFilter(View, ServerBrowser()->TypesFilter(), RowHeight, MaxEntries, EntriesPerRow, s_ScrollRegion, s_vItemIds, false, GetItemName, RenderItem);
}

CUi::EPopupMenuFunctionResult CMenus::PopupCountrySelection(void *pContext, CUIRect View, bool Active)
{
	SPopupCountrySelectionContext *pPopupContext = static_cast<SPopupCountrySelectionContext *>(pContext);
	CMenus *pMenus = pPopupContext->m_pMenus;
	CUi *pUi = pMenus->Ui();

	static CListBox s_ListBox;
	static int64_t s_PopupOpenTime = 0;
	static std::string s_LastFilter;
	s_ListBox.SetActive(Active);
	s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::POPUP);
	s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_GRID);
	// 选中填充（主题选中色）与悬停填充都由 CListBox 的胶囊格轨道统一绘制，
	// 与下拉弹层条目同源；这里不再覆盖颜色，也不补描边。
	// 多列网格也走胶囊格：行距、左右内退和单列胶囊行同一套；圆角由调用点显式
	// 传给 DoNextItem——组件对 ItemsPerRow > 1 默认仍取小圆角（多列网格的旧观感）。
	// 行距并进行高：组件的自动行距在 ItemsPerRow > 1 时按每个格子推进一格
	// （一格一行的 N 倍），网格行距会随列数放大；这里自己按行步进，网格才是
	// 严格 N 行 × 行步进，滚动条高度和实际行位一致。
	const float FlagCellHeight = 44.0f;
	const float FlagCellStep = FlagCellHeight + ui_token::list::ROW_SPACING;
	const float FlagCellRadius = ui_token::list::RowRadius(FlagCellHeight);
	s_ListBox.SetCapsuleRows(10.0f);
	// 容器内边距：格子与弹层容器四边留白，选中/悬浮胶囊不再压在圆角上（与列表同一能力）。
	s_ListBox.SetContainerPadding(ui_token::list::ContainerPadding(FlagCellHeight));
	s_ListBox.DoAutoSpacing(0.0f);
	s_ListBox.SetItemSelectionInset(ui_token::list::ROW_SPACING * 0.5f);

	if(pPopupContext->m_New)
	{
		pPopupContext->m_New = false;
		pPopupContext->m_FilterInput.Clear();
		s_ListBox.ScrollToSelected();
		s_PopupOpenTime = time_get();
		s_LastFilter.clear();
	}
	else if(s_LastFilter != pPopupContext->m_FilterInput.GetString())
	{
		s_LastFilter = pPopupContext->m_FilterInput.GetString();
		s_PopupOpenTime = time_get();
	}

	IUiContext HeaderCtx;
	HeaderCtx.m_pUi = pUi;
	static ui_widget::SSecondaryPanelLabel s_Title;
	static CButtonContainer s_CloseButton;
	ui_widget::CSecondaryPanel Panel(HeaderCtx, View, Active, ui_widget::ResolveSecondaryPanelMetrics(pUi->Screen()->w), {});
	if(Panel.Header(s_Title, s_CloseButton, Localize("Choose country flag")))
		return CUi::POPUP_CLOSE_CURRENT_AND_DESCENDANTS;
	CUIRect SearchRect, GridArea = Panel.ContentRect();
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(pUi->Screen()->w);
	GridArea.HSplitTop(Metrics.m_LineHeight, &SearchRect, &GridArea);
	GridArea.HSplitTop(Metrics.m_LineSpacing, nullptr, &GridArea);

	IUiContext SearchCtx;
	SearchCtx.m_pUi = pUi;
	SearchCtx.m_pAnim = &pMenus->GameClient()->UiRuntimeV2()->AnimRuntime();
	SearchCtx.m_pTree = &pMenus->GameClient()->UiRuntimeV2()->Tree();
	SearchCtx.m_ScopeHash = MakeUiScopeHash("browser_country_flag_popup_search");
	SearchCtx.m_FrameDt = pMenus->GameClient()->UiRuntimeV2()->FrameDt();
	ui_widget::SInputFieldOptions SearchOptions;
	SearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
	SearchOptions.m_pPlaceholder = Localize("Search country flag…");
	SearchOptions.m_Clearable = true;
	ui_widget::InputField(SearchCtx, &pPopupContext->m_FilterInput, SearchRect, SearchOptions);

	struct SFilteredFlag
	{
		const CCountryFlags::CCountryFlag *m_pEntry;
		std::optional<std::pair<int, int>> m_Match;
	};
	static std::vector<SFilteredFlag> s_vFiltered;
	s_vFiltered.clear();
	for(size_t i = 0; i < pMenus->GameClient()->m_CountryFlags.Num(); ++i)
	{
		const CCountryFlags::CCountryFlag &Entry = pMenus->GameClient()->m_CountryFlags.GetByIndex(i);
		if(!pPopupContext->m_FilterInput.IsEmpty())
		{
			const char *pMatchEnd = nullptr;
			const char *pMatchStart = str_utf8_find_nocase(Entry.m_aCountryCodeString, pPopupContext->m_FilterInput.GetString(), &pMatchEnd);
			if(pMatchStart != nullptr)
			{
				s_vFiltered.push_back({&Entry, std::make_pair((int)(pMatchStart - Entry.m_aCountryCodeString), (int)(pMatchEnd - pMatchStart))});
			}
		}
		else
		{
			s_vFiltered.push_back({&Entry, std::nullopt});
		}
	}

	const int Columns = std::clamp((int)(GridArea.w / 54.0f), 1, 14);
	int SelectedIndex = -1;
	for(size_t i = 0; i < s_vFiltered.size(); ++i)
	{
		if(s_vFiltered[i].m_pEntry->m_CountryCode == pPopupContext->m_Selection)
		{
			SelectedIndex = (int)i;
			break;
		}
	}

	// 整数行：可见行由可用高度收敛，最后一行完整可见；列表再高也不越出内容区。
	// 可见行按「可用高 − 上下两档内边距」取整：内边距不会挤掉一行。
	s_ListBox.SetVisibleRowRange(1, maximum(1, (int)std::floor(std::max(0.0f, GridArea.h - 2.0f * ui_token::list::ContainerPadding(FlagCellHeight)) / FlagCellStep)));
	s_ListBox.DoStart(FlagCellStep, s_vFiltered.size(), Columns, 1, SelectedIndex, &GridArea, false);

	for(size_t i = 0; i < s_vFiltered.size(); ++i)
	{
		const SFilteredFlag &Filtered = s_vFiltered[i];
		const CCountryFlags::CCountryFlag *pEntry = Filtered.m_pEntry;
		const bool IsSelected = pEntry->m_CountryCode == pPopupContext->m_Selection;
		const CListboxItem Item = s_ListBox.DoNextItem(pEntry, IsSelected, FlagCellRadius);
		if(!Item.m_Visible)
			continue;

		// 选中填充与白色细描边都由 CListBox 的胶囊格轨道绘制（行内内缩给出格间缝隙），
		// 这里不再补第二层描边；描边圆角随 FlagCellRadius 与胶囊格同档。
		// 内容区已含胶囊格的左右内退：旗标与国码都排在这里，纵向让 5 给上下呼吸。
		const CUIRect CellContent = Item.m_ContentRect;
		CUIRect FlagRect, Label;
		CellContent.HMargin(5.0f, &FlagRect);
		FlagRect.HSplitBottom(12.0f, &FlagRect, &Label);
		Label.HSplitTop(2.0f, nullptr, &Label);
		const float FlagWidth = std::min(CellContent.w, FlagRect.h * 2.0f);
		const float FlagHeight = FlagWidth * 0.5f;
		FlagRect = {CellContent.x + (CellContent.w - FlagWidth) * 0.5f, FlagRect.y + (FlagRect.h - FlagHeight) * 0.5f, FlagWidth, FlagHeight};
		int64_t FlagAnimStartTime = s_PopupOpenTime;
		if(s_PopupOpenTime > 0)
		{
			const int Col = (int)(i % Columns);
			const int Row = (int)(i / Columns) % 6;
			const float StaggerDelay = Col * 0.006f + Row * 0.015f;
			FlagAnimStartTime = s_PopupOpenTime + (int64_t)(StaggerDelay * time_freq());
		}
		pMenus->GameClient()->m_CountryFlags.Render(pEntry->m_CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h, FlagAnimStartTime);

		SLabelProperties Props;
		if(Filtered.m_Match.has_value())
		{
			const auto [MatchStart, MatchLen] = Filtered.m_Match.value();
			Props.m_vColorSplits.emplace_back(MatchStart, MatchLen, ui_token::color::ACCENT_PRIMARY);
		}
		pUi->DoLabel(&Label, pEntry->m_aCountryCodeString, 10.0f, TEXTALIGN_MC, Props);
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(NewSelected >= 0 && (size_t)NewSelected < s_vFiltered.size())
	{
		pPopupContext->m_Selection = s_vFiltered[NewSelected].m_pEntry->m_CountryCode;
	}
	if(s_ListBox.WasItemSelected() || s_ListBox.WasItemActivated())
	{
		g_Config.m_BrFilterCountry = 1;
		g_Config.m_BrFilterCountryIndex = pPopupContext->m_Selection;
		pMenus->Client()->ServerBrowserUpdate();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

// 本地时间状态机：折叠/展开这类「按帧时间推进」的过渡只在这里有一份缓动
// （0.15s + smoothstep），好友分类表头与服务器信息页「暴露 IP」行共用同一套。
//
// 关键约束：只有交互趟能推进（Retarget），背景/出图趟（RenderOnly）与测量趟
// （MeasurementOnly，是 RenderOnly 的子集）只读 Current()。菜单一帧里会被渲染
// 不止一趟，上一版把记账放在所有趟里：非交互趟的闸门收起时直接把 m_From/m_To
// 推到目标，等交互趟再来时 Current() 已经是终点——动画被吃掉，只剩瞬变。
static constexpr float LOCAL_EASED_PROGRESS_SEC = 0.15f;

static constexpr float LocalEasedSmoothstep(float Progress)
{
	return Progress * Progress * (3.0f - 2.0f * Progress);
}

// (from, to, startTick, now, freq) → 当前进度。纯函数：不碰任何静态状态，
// 起止点、半程与越界钳制都在下面用 static_assert 自检（等价于单元测试，
// 不需要额外测试文件）。
static constexpr float ResolveLocalEasedProgress(float From, float To, int64_t StartTick, int64_t Now, int64_t Freq)
{
	if(From == To || Freq <= 0)
		return To;
	const float ElapsedSec = (float)(Now - StartTick) / (float)Freq;
	const float Progress = std::clamp(ElapsedSec / LOCAL_EASED_PROGRESS_SEC, 0.0f, 1.0f);
	return From + (To - From) * LocalEasedSmoothstep(Progress);
}

// 推进条件：只有交互趟、且目标真的变了，才允许重起一段过渡。
static constexpr bool LocalEasedProgressShouldRetarget(bool InteractivePass, float CurrentTo, float Target)
{
	return InteractivePass && CurrentTo != Target;
}

// 下面这几条 static_assert 就是这条状态机的「单元测试」：起止点、半程与越界钳制，
// 外加「非交互趟不推进」的闸门。消息保持 ASCII——本工程 MSVC 主目标没开 /utf-8，
// 中文只出现在注释里。
static_assert(!LocalEasedProgressShouldRetarget(false, 0.0f, 1.0f), "background/measurement pass must not advance the transition");
static_assert(!LocalEasedProgressShouldRetarget(false, 1.0f, 0.0f), "background/measurement pass must not advance the transition");
static_assert(LocalEasedProgressShouldRetarget(true, 0.0f, 1.0f), "interactive pass must restart the transition when the target changes");
static_assert(!LocalEasedProgressShouldRetarget(true, 1.0f, 1.0f), "unchanged target must not restart the transition");
static_assert(ResolveLocalEasedProgress(0.0f, 1.0f, 0, 0, 1000) == 0.0f, "progress must start at From");
static_assert(ResolveLocalEasedProgress(0.0f, 1.0f, 0, 150, 1000) == 1.0f, "progress must reach To after one duration");
static_assert(ResolveLocalEasedProgress(0.0f, 1.0f, 0, 500, 1000) == 1.0f, "progress must clamp to To past the duration");
static_assert(ResolveLocalEasedProgress(1.0f, 0.0f, 0, 75, 1000) > 0.45f && ResolveLocalEasedProgress(1.0f, 0.0f, 0, 75, 1000) < 0.55f, "progress must be about halfway at half duration");
static_assert(ResolveLocalEasedProgress(0.4f, 0.4f, 0, 0, 1000) == 0.4f, "equal From/To must return the resting value");

struct SLocalEasedProgress
{
	float m_From = 1.0f;
	float m_To = 1.0f;
	int64_t m_StartTick = 0;
	float Current(int64_t Now, int64_t Freq) const { return ResolveLocalEasedProgress(m_From, m_To, m_StartTick, Now, Freq); }
	// 目标变了就重新起跑。调用点必须先用 LocalEasedProgressShouldRetarget 判断，
	// 非交互趟调一次就可能把整段过渡推到终点。
	void Retarget(float Target, int64_t Now, int64_t Freq)
	{
		m_From = Current(Now, Freq);
		m_To = Target;
		m_StartTick = Now;
	}
	// 直接落在某个静止值上（换页、换服务器这类不该播过渡的场景）。
	void SnapTo(float Value)
	{
		m_From = Value;
		m_To = Value;
		m_StartTick = 0;
	}
};

void CMenus::RenderServerbrowserInfo(CUIRect View)
{
	const CServerInfo *pSelectedServer = ServerBrowser()->SortedGet(m_SelectedIndex);

	// 本页不再另造一套尺寸：信息行、开关行和复制按钮统一吃设置卡片那份内容度量
	// （行高、正文字号、行距、分区间距都从本面板的实际内容宽推导，窄面板自动落到
	// 紧凑档），和卡片同源。之前的写法按屏幕宽取度量、再把行高压成 15px 并反推
	// 字号 (15-4)×FontmodHeight：正文因此比卡片小一档，开关却仍按标准控件高度
	// 绘制，同一页里出现两种密度。
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(View.w);
	// 行间距只按卡片语义声明：本页没有分区，只有行——行与行之间、以及进入复制
	// 按钮和记分板之前统一用 RowGap，不再借用分区的 SectionGap（那一档比行间距大，
	// 实机反馈「这几处都比行间距大」）。
	const float RowGap = ui_token::settings::RowGap(Metrics.m_LineSpacing);
	View.Margin(5.0f, &View);
	View.VSplitRight(std::min(5.0f, View.w), &View, nullptr);
	View.HSplitTop(ui_token::spacing::MD, nullptr, &View);
	if(!pSelectedServer)
	{
		View.h = std::min(View.h, 5.0f * Metrics.m_LineHeight);
		Ui()->DoLabel(&View, Localize("No server selected"), Metrics.m_BodySize, TEXTALIGN_MC);
		return;
	}

	// 收藏 / 暴露 IP 都以「本页意图」为准：点击那一帧立刻翻转，动画和行高跟着它走，
	// 不等服务器列表刷新把 m_Favorite / m_FavoriteAllowPing 回写（后台刷新至少滞后
	// 一帧以上，旧写法让目标每帧在旧值和新值之间来回，实机表现就是开关自己动来动去）。
	// 记账只在交互趟做：背景/出图/测量趟只读，不推进动画，也不回写配置。
	const bool InteractivePass = !Ui()->RenderOnly();
	// 引擎里 CFavorites::Add() 给新收藏项写死 m_AllowPing = false、Remove() 整条删除，
	// 所以「取消收藏 → 再收藏」必然回到不暴露；而 m_FavoriteAllowPing 只在当前是收藏时
	// 有意义（IsPingAllowed 对非收藏地址返回 ALL，对「收藏但不暴露」返回 NONE）。
	// 未收藏时改用持久化的 qm_browser_favorite_expose_ip 作为这次收藏的默认策略。
	const bool WasFavorite = pSelectedServer->m_Favorite != TRISTATE::NONE;
	const bool ObservedExposeIp = WasFavorite ? pSelectedServer->m_FavoriteAllowPing != TRISTATE::NONE : g_Config.m_QmBrowserFavoriteExposeIp != 0;
	static char s_aIntentServerAddress[NETADDR_MAXSTRSIZE] = "";
	static bool s_FavoriteIntent = false;
	static bool s_ExposeIpIntent = false;
	static bool s_LastObservedFavorite = false;
	static bool s_LastObservedExposeIp = false;
	static SLocalEasedProgress s_ExposeRowAnim;
	if(str_comp(s_aIntentServerAddress, pSelectedServer->m_aAddress) != 0)
	{
		// 换服务器（按地址认：列表刷新会重建条目，指针不可靠）：直接落在该服务器的
		// 真实状态上，不播过渡。
		str_copy(s_aIntentServerAddress, pSelectedServer->m_aAddress, sizeof(s_aIntentServerAddress));
		s_FavoriteIntent = s_LastObservedFavorite = WasFavorite;
		s_ExposeIpIntent = s_LastObservedExposeIp = ObservedExposeIp;
		s_ExposeRowAnim.SnapTo(s_FavoriteIntent ? 1.0f : 0.0f);
	}
	else if(InteractivePass && WasFavorite != s_LastObservedFavorite)
	{
		// 收藏状态变了：跟随它（外部改动来自服务器列表的星标 / 热键；本页点击的意图
		// 本来就等于回写值）。暴露 IP 的意图**不跟着清掉**——取消收藏再收藏时开关
		// 位置必须保持不变，由上面那条持久化配置把这个选择带过来。
		const bool NewlyFavorited = WasFavorite;
		s_LastObservedFavorite = WasFavorite;
		s_FavoriteIntent = WasFavorite;
		// 外部新建的收藏项（列表星标 / 热键）同样是引擎默认的「不暴露」，按记住的
		// 策略补一次，否则第四个问题（开关位置被引擎默认值悄悄改掉）只会换个入口复发。
		if(NewlyFavorited && !ObservedExposeIp && g_Config.m_QmBrowserFavoriteExposeIp != 0)
			Favorites()->AllowPing(pSelectedServer->m_aAddresses, pSelectedServer->m_NumAddresses, true);
		s_LastObservedExposeIp = ObservedExposeIp;
	}
	else if(InteractivePass && ObservedExposeIp != s_LastObservedExposeIp)
	{
		// 本页之外改了暴露策略（控制台的 add_favorite / allow_ping）：跟随它。
		s_LastObservedExposeIp = ObservedExposeIp;
		s_ExposeIpIntent = ObservedExposeIp;
	}

	// 暴露 IP 行的让位：与好友分类同一台状态机、同一条 smoothstep，只有交互趟推进。
	const int64_t ExposeNow = time_get();
	const int64_t ExposeFreq = maximum<int64_t>(1, time_freq());
	const float ExposeTarget = s_FavoriteIntent ? 1.0f : 0.0f;
	if(LocalEasedProgressShouldRetarget(!Ui()->RenderOnly(), s_ExposeRowAnim.m_To, ExposeTarget))
		s_ExposeRowAnim.Retarget(ExposeTarget, ExposeNow, ExposeFreq);
	// 动效关掉（或测量趟）时直接给目标值，状态机本身不因闸门被推着走。
	const float ExposeRowAnim = g_Config.m_QmUiMotionLevel > 0 && !Ui()->MeasurementOnly() ? s_ExposeRowAnim.Current(ExposeNow, ExposeFreq) : ExposeTarget;

	const char *apLabels[] = {Localize("Version"), Localize("Game type"), Localize("Ping")};
	float LabelWidth = 0.0f;
	for(const char *pLabel : apLabels)
		LabelWidth = std::max(LabelWidth, TextRender()->TextWidth(Metrics.m_BodySize, pLabel));
	LabelWidth = std::min(LabelWidth + RowGap, View.w * 0.45f);
	// 行前间距，与卡片 Flow.Control(..., SpacingBefore) 同一语义：首行不加，
	// 之后每行先让一档 RowGap，行与行之间只有这一档。
	const auto DrawInfoRow = [&](const char *pLabel, const char *pValue, float SpacingBefore, SLabelProperties ValueProps = {}) {
		if(SpacingBefore > 0.0f)
			View.HSplitTop(SpacingBefore, nullptr, &View);
		CUIRect Row, Label, Value;
		View.HSplitTop(Metrics.m_LineHeight, &Row, &View);
		Row.VSplitLeft(LabelWidth, &Label, &Value);
		// 标签与右对齐数值之间的最小间隔同样取 RowGap，不借用分区间距。
		Value.VSplitLeft(std::min(RowGap, Value.w), nullptr, &Value);
		SLabelProperties LabelProps;
		LabelProps.m_MaxWidth = Label.w;
		LabelProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Label, pLabel, Metrics.m_BodySize, TEXTALIGN_ML, LabelProps);
		ValueProps.m_MaxWidth = Value.w;
		ValueProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Value, pValue, Metrics.m_BodySize, TEXTALIGN_MR, ValueProps);
	};
	DrawInfoRow(apLabels[0], pSelectedServer->m_aVersion, 0.0f);
	SLabelProperties GameTypeLabelProps;
	if(g_Config.m_UiColorizeGametype)
		GameTypeLabelProps.SetColor(pSelectedServer->m_GametypeColor);
	DrawInfoRow(apLabels[1], pSelectedServer->m_aGameType, RowGap, GameTypeLabelProps);
	SLabelProperties PingLabelProps;
	if(g_Config.m_UiColorizePing && CServerInfo::IsUsableLatency(pSelectedServer->m_Latency))
		PingLabelProps.SetColor(GetServerListPingColor(pSelectedServer, ServerBrowser()->GetCurrentClientLocation()));
	char aPingLabel[8];
	CServerInfo::FormatLatency(aPingLabel, pSelectedServer->m_Latency);
	DrawInfoRow(apLabels[2], aPingLabel, RowGap, PingLabelProps);
	View.HSplitTop(RowGap, nullptr, &View);

	// 「收藏」与「暴露 IP」两行都走设置卡片的开关行同一入口（DoSettingsButton_CheckBox
	// → ui_widget::DrawToggle）：左侧标签 + 右侧 Toggle，点击整行或开关都能切。
	// 行高直接取同一份度量的标准控件高度，不再为开关单独放大——开关轨道/滑块的
	// 尺寸本来就由「行高 + 正文字号」推出，行高统一即开关统一。
	const float ToggleRowHeight = Metrics.m_LineHeight;
	CUIRect Button;
	static int s_AddFavButton = 0;
	int Favorite = s_FavoriteIntent ? 1 : 0;
	View.HSplitTop(ToggleRowHeight, &Button, &View);
	// 标签带上作用对象：「收藏服务器」，和相邻的「暴露 IP」同为「动作 + 对象」口径，
	// 不再只有一个动作词。语义、配置项与提示文案都不变，只换显示文案。
	if(DoSettingsButton_CheckBox(-1, -1, -1, &s_AddFavButton, "Favorite", Localize("Favorite server"), Favorite, &Button, SLabelProperties{}, !Ui()->RenderOnly(), Metrics.m_BodySize))
	{
		s_FavoriteIntent = !s_FavoriteIntent;
		if(s_FavoriteIntent)
		{
			Favorites()->Add(pSelectedServer->m_aAddresses, pSelectedServer->m_NumAddresses);
			// 列表星标据此播一次进场；表只活到那一行被渲染为止。
			if(gs_FavoriteStarGrowPending.size() >= 64)
				gs_FavoriteStarGrowPending.clear();
			gs_FavoriteStarGrowPending.insert(pSelectedServer->m_aAddress);
			// 引擎新建的收藏项一律 m_AllowPing = false：把本页维护的暴露意图写回去，
			// 否则「打开收藏 → 关闭收藏 → 再打开」会把开关位置悄悄改回关闭（用户点名）。
			// LAN 另有一档历史行为：添加时自动允许 ping，否则拿不到服务器信息。
			if(s_ExposeIpIntent || g_Config.m_UiPage == PAGE_LAN)
			{
				Favorites()->AllowPing(pSelectedServer->m_aAddresses, pSelectedServer->m_NumAddresses, true);
				s_ExposeIpIntent = true;
			}
		}
		else
		{
			Favorites()->Remove(pSelectedServer->m_aAddresses, pSelectedServer->m_NumAddresses);
		}
		Client()->ServerBrowserUpdate();
	}

	// 收藏后向下展开「暴露 IP」行：进度是上面对 s_ExposeRowAnim 求出的同一个值，
	// 复制按钮与玩家列表随实际行高让位（不再有第二套缓动）。
	if(ExposeRowAnim > 0.0f)
	{
		static int s_LeakIpButton = 0;
		int ExposeIp = s_ExposeIpIntent ? 1 : 0;
		// 与「收藏」同一套行几何与开关绘制（语义不变：仍然是 m_FavoriteAllowPing）。
		const float Height = ToggleRowHeight;
		View.HSplitTop(RowGap * ExposeRowAnim, nullptr, &View);
		View.HSplitTop(Height * ExposeRowAnim, &Button, &View);
		if(Button.h > 2.0f)
		{
			CUiScopedControlAlphaScale ExposeAlpha(Ui(), ExposeRowAnim);
			const CUIRect Clip = Button;
			Ui()->ClipEnable(&Clip);
			Button.h = Height;
			if(DoSettingsButton_CheckBox(-1, -1, -1, &s_LeakIpButton, "Leak IP", Localize("Leak IP"), ExposeIp, &Button, SLabelProperties{}, !Ui()->RenderOnly(), Metrics.m_BodySize))
			{
				s_ExposeIpIntent = !s_ExposeIpIntent;
				Favorites()->AllowPing(pSelectedServer->m_aAddresses, pSelectedServer->m_NumAddresses, s_ExposeIpIntent);
				// 记住这次选择：它就是新建收藏项的默认暴露策略，也是「取消收藏再收藏」
				// 之后开关位置保持不变的那份持久化状态（引擎侧记录会随 Remove 丢掉）。
				// 先写收藏项再落盘，一次 Save 同时带上条目与这条配置。
				g_Config.m_QmBrowserFavoriteExposeIp = s_ExposeIpIntent ? 1 : 0;
				ConfigManager()->Save();
				Client()->ServerBrowserUpdate();
			}
			// 行说明保持不变（它不是 g_Config 项，DoSettingsButton_CheckBox 内部的
			// 配置提示查不到它，这里显式挂一次）。
			GameClient()->m_Tooltips.DoToolTip(&s_LeakIpButton, &Button, Localize("Leaks your IP address to this server by pinging it, also acquiring the latency in the process"));
			Ui()->ClipDisable();
		}
	}

	View.HSplitTop(RowGap, nullptr, &View);
	View.HSplitTop(Metrics.m_ButtonHeight, &Button, &View);
	static CButtonContainer s_CopyButton;
	// 次级按钮与设置卡片里的同档按钮是同一入口（ui_widget::SecondaryButton）：
	// 字号按 Ctx.m_UiScale 取同一档正文字号，胶囊半径也与卡片一致，不再用菜单
	// 按钮的「行高 × FontmodHeight」自行推字号。
	if(ui_widget::SecondaryButton(SettingsUiContext("server_browser_info_copy", Metrics.m_UiScale), &s_CopyButton, Localize("Copy info"), Button))
	{
		char aInfo[512];
		str_format(aInfo, sizeof(aInfo),
			"%s\n"
			"Address: ddnet://%s\n"
			"Map: %s\n",
			pSelectedServer->m_aName,
			pSelectedServer->m_aAddress,
			pSelectedServer->m_aMap);
		Input()->SetClipboardText(aInfo);
	}
	View.HSplitTop(RowGap, nullptr, &View);
	// 记分板与上面的「复制服务器信息」按钮共用同一个左缘、同一条内容宽，不再做
	// `View.x -= 5 / View.w += 15` 再由 Scoreboard 里 VSplitLeft(5) 抵消的往返——
	// 两组内边距对不齐时卡片会左右错开，而宽度也会跟着那 10 个单位走。
	// 横向让位只剩列表组件自己的滚动条那一列（RowReserveWidth），与可见行数无关：
	// 暴露 IP 行展开少一行时，卡片宽度保持不变，只有可见行数变。
	View.h = std::max(0.0f, View.h);
	RenderServerbrowserInfoScoreboard(View, pSelectedServer);
}

void CMenus::RenderServerbrowserInfoScoreboard(CUIRect View, const CServerInfo *pSelectedServer)
{
	const float FontSize = 10.0f;
	// 玩家行与服务器列表同一套胶囊行：行距并进行高（可见范围按整数行收敛），
	// 行内左右内边距由组件按字号派生，皮肤/旗标跟着内容区排。
	const float PlayerRowHeight = 25.0f;
	const float PlayerRowStep = PlayerRowHeight + ui_token::list::ROW_SPACING;

	static CListBox s_ListBox;
	s_ListBox.SetCapsuleRows(FontSize);
	// 玩家行是本页唯一能拿到真实内容高度的列表：没有滚动条时行胶囊占满整宽，
	// 滚动条出现的那一刻横向让出一列（带过渡），不再一直空着一条轨道。
	s_ListBox.SetReserveRightWhenOverflowing(true);
	s_ListBox.DoAutoSpacing(0.0f);
	s_ListBox.SetItemSelectionInset(ui_token::list::ROW_SPACING * 0.5f);
	s_ListBox.SetVisibleRowRange(1, maximum(1, (int)std::floor(std::max(0.0f, View.h) / PlayerRowStep)));
	s_ListBox.DoStart(PlayerRowStep, (int)pSelectedServer->m_vClients.size(), 1, 3, -1, &View, false, IGraphics::CORNER_NONE);

	for(size_t i = 0; i < pSelectedServer->m_vClients.size(); i++)
	{
		const CServerInfo::CClient &CurrentClient = pSelectedServer->m_vClients[i];
		const CListboxItem Item = s_ListBox.DoNextItem(&CurrentClient);
		if(!Item.m_Visible)
			continue;
		auto GaussianBlurSuppression = Item.SuppressGaussianBlur();

		CUIRect Skin, Name, Clan, Score, Flag;
		const ColorRGBA Color = PlayerBackgroundColor(CurrentClient.m_FriendState == IFriends::FRIEND_PLAYER, CurrentClient.m_FriendState == IFriends::FRIEND_CLAN, CurrentClient.m_Afk, false, false);
		// 好友/战队/离开色块与胶囊行同形同内缩，不再是一块方角底色。
		CUIRect RowRect = Item.m_Rect;
		RowRect.Margin(ui_token::list::ROW_SPACING * 0.5f, &RowRect);
		DrawRoundedSurface(Ui(), RowRect, Color, ColorRGBA(), ui_token::list::RowRadius(PlayerRowHeight));
		Name = Item.m_ContentRect;
		Name.HMargin(ui_token::list::ROW_SPACING * 0.5f, &Name);
		Name.VSplitLeft(34.0f, &Score, &Name);
		Name.VSplitLeft(18.0f, &Skin, &Name);
		Name.VSplitRight(26.0f, &Name, &Flag);
		Flag.HMargin(6.0f, &Flag);
		Name.HSplitTop(12.0f, &Name, &Clan);

		// score
		char aTemp[16];
		if(!CurrentClient.m_Player)
		{
			str_copy(aTemp, "SPEC");
		}
		else if(pSelectedServer->m_ClientScoreKind == CServerInfo::CLIENT_SCORE_KIND_POINTS)
		{
			str_format(aTemp, sizeof(aTemp), "%d", CurrentClient.m_Score);
		}
		else
		{
			std::optional<int> Time = {};

			if(pSelectedServer->m_ClientScoreKind == CServerInfo::CLIENT_SCORE_KIND_TIME_BACKCOMPAT)
			{
				const int TempTime = absolute(CurrentClient.m_Score);
				if(TempTime != 0 && TempTime != 9999)
					Time = TempTime;
			}
			else
			{
				// CServerInfo::CLIENT_SCORE_KIND_POINTS
				if(CurrentClient.m_Score >= 0)
					Time = CurrentClient.m_Score;
			}

			if(Time.has_value())
			{
				str_time((int64_t)Time.value() * 100, TIME_HOURS, aTemp, sizeof(aTemp));
			}
			else
			{
				aTemp[0] = '\0';
			}
		}

		Ui()->DoLabel(&Score, aTemp, FontSize, TEXTALIGN_ML);

		// render tee if available
		if(CurrentClient.m_aSkin[0] != '\0')
		{
			const CTeeRenderInfo TeeInfo = GetTeeRenderInfo(vec2(Skin.w, Skin.h), CurrentClient.m_aSkin, CurrentClient.m_CustomSkinColors, CurrentClient.m_CustomSkinColorBody, CurrentClient.m_CustomSkinColorFeet);
			RenderTools()->RenderIdleTeeAtCenter(&TeeInfo, vec2(Skin.x + TeeInfo.m_Size / 2.0f, Skin.y + Skin.h / 2.0f), CurrentClient.m_Afk ? EMOTE_BLINK : EMOTE_NORMAL);
			Ui()->DoButtonLogic(&CurrentClient.m_aSkin, 0, &Skin, BUTTONFLAG_NONE);
			GameClient()->m_Tooltips.DoToolTip(&CurrentClient.m_aSkin, &Skin, CurrentClient.m_aSkin);
		}
		else if(CurrentClient.m_aaSkin7[protocol7::SKINPART_BODY][0] != '\0')
		{
			const CTeeRenderInfo TeeInfo = GetTeeRenderInfo(vec2(Skin.w, Skin.h), "default", false, 0, 0);
			RenderTools()->RenderIdleTeeAtCenter(&TeeInfo, vec2(Skin.x + TeeInfo.m_Size / 2.0f, Skin.y + Skin.h / 2.0f), CurrentClient.m_Afk ? EMOTE_BLINK : EMOTE_NORMAL);
		}

		// name
		CTextCursor NameCursor;
		NameCursor.SetPosition(vec2(Name.x, Name.y + (Name.h - (FontSize - 1.0f)) / 2.0f));
		NameCursor.m_FontSize = FontSize - 1.0f;
		NameCursor.m_Flags |= TEXTFLAG_STOP_AT_END;
		NameCursor.m_LineWidth = Name.w;
		const char *pName = CurrentClient.m_aName;
		bool Printed = false;
		if(g_Config.m_BrFilterString[0])
			Printed = PrintHighlighted(pName, [&](const char *pFilteredStr, const int FilterLen) {
				TextRender()->TextEx(&NameCursor, pName, (int)(pFilteredStr - pName));
				TextRender()->TextColor(gs_HighlightedTextColor);
				TextRender()->TextEx(&NameCursor, pFilteredStr, FilterLen);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				TextRender()->TextEx(&NameCursor, pFilteredStr + FilterLen, -1);
			});
		if(!Printed)
			TextRender()->TextEx(&NameCursor, pName, -1);

		// clan
		CTextCursor ClanCursor;
		ClanCursor.SetPosition(vec2(Clan.x, Clan.y + (Clan.h - (FontSize - 2.0f)) / 2.0f));
		ClanCursor.m_FontSize = FontSize - 2.0f;
		ClanCursor.m_Flags |= TEXTFLAG_STOP_AT_END;
		ClanCursor.m_LineWidth = Clan.w;
		const char *pClan = CurrentClient.m_aClan;
		Printed = false;
		if(g_Config.m_BrFilterString[0])
			Printed = PrintHighlighted(pClan, [&](const char *pFilteredStr, const int FilterLen) {
				TextRender()->TextEx(&ClanCursor, pClan, (int)(pFilteredStr - pClan));
				TextRender()->TextColor(0.4f, 0.4f, 1.0f, 1.0f);
				TextRender()->TextEx(&ClanCursor, pFilteredStr, FilterLen);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				TextRender()->TextEx(&ClanCursor, pFilteredStr + FilterLen, -1);
			});
		if(!Printed)
			TextRender()->TextEx(&ClanCursor, pClan, -1);

		// flag
		const int CountryCode = g_Config.m_QmStreamerScoreboardDefaultFlags ? -1 : CurrentClient.m_Country;
		GameClient()->m_CountryFlags.Render(CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), Flag.x, Flag.y, Flag.w, Flag.h);
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(s_ListBox.WasItemSelected())
	{
		const CServerInfo::CClient &SelectedClient = pSelectedServer->m_vClients[NewSelected];
		if(SelectedClient.m_FriendState == IFriends::FRIEND_PLAYER)
		{
			GameClient()->Friends()->RemoveFriend(SelectedClient.m_aName, SelectedClient.m_aClan);
		}
		else
		{
			const int DefaultCategoryIndex = maximum(0, GameClient()->Friends()->FindCategory(GameClient()->Friends()->DefaultCategory()));
			if(m_FriendAddCategoryIndex < 0 || m_FriendAddCategoryIndex >= GameClient()->Friends()->NumCategories() || IsClanMembersCategory(GameClient()->Friends()->GetCategory(m_FriendAddCategoryIndex)))
				m_FriendAddCategoryIndex = DefaultCategoryIndex;
			GameClient()->Friends()->AddFriend(SelectedClient.m_aName, SelectedClient.m_aClan, GameClient()->Friends()->GetCategory(m_FriendAddCategoryIndex));
		}
		FriendlistOnUpdate();
		Client()->ServerBrowserUpdate();
	}
}

void CMenus::RenderServerbrowserFriends(CUIRect View)
{
	const bool MenuUiPerfEnabled = QmPerfEnabled();
	const auto MenuUiStartTime = MenuUiPerfEnabled ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();
	// 好友卡片进场淡入的状态：记录每张卡片首次出现的时间；面板重新出现时整列重播，
	// 与列表组件「重新显示即重播进场」保持一致。
	const int64_t FadeNow = time_get();
	const int64_t FadeFreq = maximum<int64_t>(1, time_freq());
	static int64_t s_FriendListLastDraw = 0;
	static std::unordered_map<uintptr_t, int64_t> s_FriendCardFirstSeen;
	if(s_FriendListLastDraw == 0 || FadeNow - s_FriendListLastDraw > FadeFreq / 20 || s_FriendCardFirstSeen.size() > 512)
		s_FriendCardFirstSeen.clear();
	s_FriendListLastDraw = FadeNow;
	const float FontSize = 10.0f;
	const float SpacingH = 2.0f;
	// 标签按译文实测宽度收窄，不再常驻预留 32 单位；只在真正跟随时占位。
	// 高度、内边距、字号都交给 ui_tag（固定档），右缘让出 6 单位，避免胶囊边框
	// 和卡片彩虹描边贴在一起。
	constexpr float FollowTagRightInset = 6.0f;
	constexpr float FollowTagGap = 4.0f;
	const float FollowTagWidth = ui_tag::MeasureWidth(TextRender(), Localize("Follow"));
	const float FollowTagSlotWidth = FollowTagWidth + FollowTagRightInset;
	constexpr float ActionFadeInSec = 0.08f;
	constexpr float ActionFadeOutDelaySec = 0.15f;

	CUIRect List, ServerFriends;
	View.HSplitBottom(92.0f, &List, &ServerFriends);
	List.HSplitTop(5.0f, nullptr, &List);

	FriendlistOnUpdate();
	const int NumCategories = maximum(1, GameClient()->Friends()->NumCategories());

	m_BrowserFriendList.Update(*GameClient()->Friends(), *ServerBrowser(), g_Config.m_ClFriendsIgnoreClan != 0);
	const auto &vvFriends = m_BrowserFriendList.Groups();

	bool OpenRemovePopup = false;
	static CScrollRegion s_FriendsMoveCategoryPopupScrollRegion;
	static CScrollRegion s_FriendsActionPopupScrollRegion;
	bool FollowTargetOnline = false;
	const char *pFollowTargetAddress = "";
	if(GameClient()->m_PieMenu.IsFollowing())
		StopFriendAutoFollow(m_FriendAutoFollowState);
	for(const auto &vFriends : vvFriends)
	{
		for(const auto &Friend : vFriends)
		{
			if(Friend.ServerInfo() == nullptr)
				continue;
			if(str_comp(Friend.Name(), m_FriendAutoFollowState.m_aName) == 0 && str_comp(Friend.Clan(), m_FriendAutoFollowState.m_aClan) == 0)
			{
				FollowTargetOnline = true;
				pFollowTargetAddress = Friend.ServerInfo()->m_aAddress;
				break;
			}
		}
		if(FollowTargetOnline)
			break;
	}
	char aFollowConnectAddress[NETADDR_MAXSTRSIZE];
	if(FriendAutoFollowStep(m_FriendAutoFollowState, FollowTargetOnline, pFollowTargetAddress, Client()->GlobalTime(), g_Config.m_QmFriendAutoFollowDelay, 2, aFollowConnectAddress, sizeof(aFollowConnectAddress)))
	{
		str_copy(g_Config.m_UiServerAddress, aFollowConnectAddress);
		Connect(g_Config.m_UiServerAddress, EConnectIntent::AutoFollow);
	}

	int TotalFriendItems = 0;
	for(const auto &vFriends : vvFriends)
		TotalFriendItems += (int)vFriends.size();
	m_vFriendTooltipText.clear();
	m_vFriendTooltipText.resize(TotalFriendItems);
	int VisibleFriendItems = 0;

	// friends list
	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	SQmScrollRequest ScrollRequest;
	ScrollRequest.m_Profile = EQmScrollProfile::MENU_LIST;
	ScrollRequest.m_RowExtent = 24.0f;
	ScrollRequest.m_RowsPerStep = 3;
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest);
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	// 好友列表与非覆盖式面板共用同一档轨道：**没有滚动条时内容占满全宽**，
	// 轨道出现时占位宽度动画过渡（m_AnimateScrollbarReserve），卡片与分类表头
	// 连续让位而不是整段横跳。展开/折叠动画期间内容高度逐帧变化，占位动画把
	// 「本帧是否溢出」的跳变吸收掉，所以既不需要恒定让位，也不会瞬移。
	ApplyFriendsScrollbar(ScrollParams, ui_token::radius::CARD);
	s_ScrollRegion.Begin(&List, &ScrollOffset, &ScrollParams);
	List.y += ScrollOffset.y;

	// 视口宽度直接取滚动区裁剪区：占位（含动画中的占位）已经由 Begin 从裁剪区里扣掉，
	// 拖动悬停判定与插入提示线都按内容宽算，不会伸到轨道下面去。
	CUIRect ListViewport = *s_ScrollRegion.ClipRect();
	const float UiScale = 1.0f;
	const float CategoryDropPreviewThickness = std::clamp(3.0f * UiScale, 2.0f, 4.0f);
	const float CategoryDragOutlineThickness = std::clamp(2.0f * UiScale, 1.0f, 2.0f);
	const ColorRGBA CategoryDropPreviewColor(0.2f, 0.9f, 0.4f, 0.9f);
	const ColorRGBA CategoryDragOutlineColor(1.0f, 0.85f, 0.2f, 0.9f);
	const ColorRGBA CategoryDragGhostColor(0.08f, 0.09f, 0.12f, 0.55f);

	static SFriendsCategoryDragState s_CategoryDragState;
	static SFriendsPlayerDragState s_FriendDragState;

	struct SFriendsCategoryDropPreview
	{
		bool m_Active = false;
		bool m_Valid = false;
		int m_DraggedIndex = -1;
		int m_InsertIndex = 0;
		CUIRect m_LineRect = {};
	};
	static SFriendsCategoryDropPreview s_CategoryDropPreview;

	struct SFriendsCategoryHeaderInfo
	{
		int m_CategoryIndex = -1;
		CUIRect m_Rect = {};
	};

	auto ResetCategoryDragState = [&]() {
		s_CategoryDragState = SFriendsCategoryDragState();
		s_CategoryDropPreview = SFriendsCategoryDropPreview();
	};
	auto ResetFriendDragState = [&]() {
		if(Ui()->IsActiveItem(&s_FriendDragState) || (s_FriendDragState.m_pPressedItem != nullptr && Ui()->IsActiveItem(s_FriendDragState.m_pPressedItem)))
			Ui()->SetActiveItem(nullptr);
		s_FriendDragState = SFriendsPlayerDragState();
	};

	auto DrawCategoryDragOutline = [&](const CUIRect &Rect) {
		CUIRect Line = Rect;
		Line.HSplitTop(CategoryDragOutlineThickness, &Line, nullptr);
		Line.Draw(CategoryDragOutlineColor, IGraphics::CORNER_NONE, 0.0f);

		Line = Rect;
		Line.HSplitBottom(CategoryDragOutlineThickness, nullptr, &Line);
		Line.Draw(CategoryDragOutlineColor, IGraphics::CORNER_NONE, 0.0f);

		Line = Rect;
		Line.VSplitLeft(CategoryDragOutlineThickness, &Line, nullptr);
		Line.Draw(CategoryDragOutlineColor, IGraphics::CORNER_NONE, 0.0f);

		Line = Rect;
		Line.VSplitRight(CategoryDragOutlineThickness, nullptr, &Line);
		Line.Draw(CategoryDragOutlineColor, IGraphics::CORNER_NONE, 0.0f);
	};

	auto MoveCategoryExpandedState = [&](int FromIndex, int ToIndex) {
		if(FromIndex < 0 || ToIndex < 0 || FromIndex == ToIndex || FromIndex >= (int)m_vFriendsCategoryExpanded.size() || ToIndex >= (int)m_vFriendsCategoryExpanded.size())
			return;

		const unsigned char Expanded = m_vFriendsCategoryExpanded[FromIndex];
		if(FromIndex < ToIndex)
		{
			for(int Index = FromIndex; Index < ToIndex; ++Index)
				m_vFriendsCategoryExpanded[Index] = m_vFriendsCategoryExpanded[Index + 1];
		}
		else
		{
			for(int Index = FromIndex; Index > ToIndex; --Index)
				m_vFriendsCategoryExpanded[Index] = m_vFriendsCategoryExpanded[Index - 1];
		}
		m_vFriendsCategoryExpanded[ToIndex] = Expanded;
	};

	if(s_CategoryDragState.m_DraggingIndex >= NumCategories || s_CategoryDragState.m_PressedIndex >= NumCategories)
		ResetCategoryDragState();
	if(s_CategoryDragState.m_PressedIndex >= 0 &&
		(!Ui()->IsActiveItem(&m_vFriendsCategoryExpanded[s_CategoryDragState.m_PressedIndex]) || Ui()->IsPopupOpen()))
		ResetCategoryDragState();
	if(s_CategoryDragState.Update(vec2(Ui()->MouseX(), Ui()->MouseY()), Ui()->MouseButton(0)))
	{
		List.y -= ScrollOffset.y;
		s_ScrollRegion.SetScrollOffsetY(0.0f);
	}
	const bool DraggingAnyHeader = s_CategoryDragState.m_DraggingIndex >= 0;
	if(s_FriendDragState.m_pPressedItem != nullptr)
	{
		const void *pDragActiveId = s_FriendDragState.m_Dragging || Ui()->IsActiveItem(&s_FriendDragState) ? &s_FriendDragState : s_FriendDragState.m_pPressedItem;
		if(Ui()->IsPopupOpen() || !Ui()->CheckActiveItem(pDragActiveId))
			ResetFriendDragState();
		else if(s_FriendDragState.Update(vec2(Ui()->MouseX(), Ui()->MouseY()), Ui()->MouseButton(0)))
		{
			// 列表行临时隐藏后使用独立 ID 持续拖动，避免松手被识别为加入服务器。
			Ui()->SetActiveItem(&s_FriendDragState);
			List.y -= ScrollOffset.y;
			s_ScrollRegion.SetScrollOffsetY(0.0f);
		}
	}
	const bool DraggingFriend = s_FriendDragState.m_Dragging;
	int FriendDropCategoryIndex = -1;
	if(DraggingFriend)
		Ui()->SetHotItem(&s_FriendDragState);

	std::vector<SFriendsCategoryHeaderInfo> vCategoryHeaders;
	vCategoryHeaders.reserve(NumCategories);
	m_vFriendsCategoryManageButtons.resize(NumCategories);

	// 折叠/展开进度用文件级的本地时间状态机（同一份 0.15s + smoothstep）。菜单会被
	// 背景/出图趟与测量趟各渲染一次，动画运行时不一定逐帧喂值，所以不走 ResolveUiAnimValue；
	// 状态只在交互趟推进，其余趟只读。
	static std::vector<SLocalEasedProgress> s_vCategoryExpand;

	char aBuf[256];
	int FriendTooltipIndex = 0;
	for(int CategoryIndex = 0; CategoryIndex < NumCategories; ++CategoryIndex)
	{
		// header
		CUIRect Header, HeaderAction, GroupIcon, GroupLabel, ManageButton;
		List.HSplitTop(ms_ListheaderHeight, &Header, &List);
		s_ScrollRegion.AddRect(Header);
		vCategoryHeaders.push_back({CategoryIndex, Header});
		const char *pCategoryName = GameClient()->Friends()->GetCategory(CategoryIndex);
		// 表头矩形是「胶囊 / 切换热区 / 齿轮 / 图标 + 标签」四段派生的唯一来源：两处
		// Split 都拿同一条未改写的 Header 当输入。此前先 Header.VSplitLeft(...) 改写
		// 自身，再把改窄后的矩形拿去切齿轮——齿轮位置只是两处宽度差刚好抵消才对得上；
		// 拖拽描边更糟：用的是改写后的 Header，左端整整少一个图标宽（15），
		// 和胶囊本身不同宽。
		SplitFriendsCategoryHeaderRects(Header, &HeaderAction, &ManageButton);
		const bool HeaderInside = Ui()->MouseHovered(&HeaderAction);
		const bool FriendDropHovered = DraggingFriend && HeaderInside && Ui()->MouseHovered(&ListViewport);
		const bool FriendDropAllowed = s_FriendDragState.CanDropTo(pCategoryName);
		if(FriendDropHovered && FriendDropAllowed)
			FriendDropCategoryIndex = CategoryIndex;
		const bool DraggingThisHeader = s_CategoryDragState.m_DraggingIndex == CategoryIndex;
		const bool HeaderHovered = HeaderInside || DraggingThisHeader;
		const bool PopupOpen = Ui()->IsPopupOpen(&m_FriendsCategoryPopupContext) && m_FriendsCategoryPopupContext.m_CategoryIndex == CategoryIndex;
		ColorRGBA HeaderColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		if(str_comp_nocase(pCategoryName, IFriends::DEFAULT_CATEGORY) == 0)
			HeaderColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClFriendsListFriendColor));
		else if(IsClanMembersCategory(pCategoryName))
			HeaderColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClFriendsListClanColor));
		// 三行填充共用同一档 alpha（静止 0.25 / 悬停或弹层 0.4）：基色只承担「好友 /
		// 战队 / 中立」这一层语义，不再让某一行单独改 alpha。半透明胶囊两端融进背景
		// 的程度一变，同一张图里的三个表头就会被读成三个不同长度——这正是「宽度不
		// 一致」的观感来源；宽度本身由上面那次恒定让位保证（有无轨道、展开多少都一样）。
		HeaderColor.a = HeaderHovered || PopupOpen ? 0.4f : 0.25f;
		if(FriendDropHovered)
			HeaderColor = FriendDropAllowed ? ColorRGBA(0.2f, 0.9f, 0.4f, 0.5f) : ColorRGBA(0.9f, 0.25f, 0.2f, 0.4f);
		// 分类表头是横条，端头直接用半圆：和页面里其它胶囊背景同一种形状。
		DrawRoundedSurface(Ui(), Header, HeaderColor, ColorRGBA(), ui_token::radius::PILL);
		CUIRect HeaderLabel = Header;
		HeaderLabel.VSplitLeft(HeaderLabel.h, &GroupIcon, &GroupLabel);
		GroupIcon.Margin(2.0f, &GroupIcon);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->TextColor(HeaderHovered ? TextRender()->DefaultTextColor() : ColorRGBA(0.6f, 0.6f, 0.6f, 1.0f));
		// 分组展开 ⇄ 折叠：加/减方框形变（外框常量、加号竖臂压平），拖拽表头时按折叠显示。
		// 展开进度 ExpandT 是唯一事实：图标形变跟意图，行高与下方内容让位跟进度。
		const bool WantsExpanded = m_vFriendsCategoryExpanded[CategoryIndex] && !DraggingAnyHeader && !DraggingFriend;
		const bool ExpandMotion = g_Config.m_QmUiMotionLevel > 0 && !Ui()->MeasurementOnly();
		if(s_vCategoryExpand.size() < (size_t)NumCategories)
			s_vCategoryExpand.resize(NumCategories, SLocalEasedProgress());
		SLocalEasedProgress &ExpandAnim = s_vCategoryExpand[CategoryIndex];
		const float ExpandTarget = WantsExpanded ? 1.0f : 0.0f;
		// 关键：只有交互趟推进状态机。背景/出图/测量趟同样会走这段代码，若它们把
		// m_From/m_To 推到目标，交互趟再来时 Current() 已经是终点——展开动画被吃掉，
		// 只剩瞬变（实机「完全没有动画」就是这条路径）。
		if(LocalEasedProgressShouldRetarget(!Ui()->RenderOnly(), ExpandAnim.m_To, ExpandTarget))
			ExpandAnim.Retarget(ExpandTarget, FadeNow, FadeFreq);
		const float ExpandT = ExpandMotion ? ExpandAnim.Current(FadeNow, FadeFreq) : ExpandTarget;
		const bool GroupExpanded = ExpandT > 0.001f;
		IUiContext GroupIconCtx = ui_widget::ControlContext(Ui());
		ui_widget::SIconSwapBinding GroupIconBinding = ui_widget::ResolveIconSwapBinding(
			GroupIconCtx, EQmIconSwapPair::SQUARE_PLUS_MINUS,
			EQmIcon::SQUARE_PLUS, FONT_ICON_SQUARE_PLUS,
			EQmIcon::SQUARE_MINUS, FONT_ICON_SQUARE_MINUS,
			WantsExpanded ? 1.0f : 0.0f, str_quickhash("friends-category-icon"), pCategoryName);
		ui_widget::DrawIconSwapBinding(GroupIconCtx, QmIconLabelRect(GroupIcon, GroupIcon.h * CUi::ms_FontmodHeight, TEXTALIGN_MC), TextRender()->GetTextColor(), &GroupIconBinding);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		// 标签右缘收到齿轮左边：两者都来自同一条 Header 派生（见函数开头那次 Split）。
		GroupLabel.w = maximum(ManageButton.x - GroupLabel.x, 0.0f);
		str_format(aBuf, sizeof(aBuf), "%s (%d)", LocalizeFriendsCategory(pCategoryName), (int)vvFriends[CategoryIndex].size());
		Ui()->DoLabel(&GroupLabel, aBuf, FontSize, TEXTALIGN_ML);
		if(DraggingThisHeader)
			DrawCategoryDragOutline(Header);

		auto OpenCategoryManagePopup = [&]() {
			m_FriendsCategoryPopupContext.m_pMenus = this;
			m_FriendsCategoryPopupContext.m_CategoryIndex = CategoryIndex;
			m_FriendsCategoryPopupContext.m_Mode = CFriendsCategoryPopupContext::MODE_ACTIONS;
			m_FriendsCategoryPopupContext.m_NameInput.Clear();
			const CUIRect Panel = CMenus::SecondaryPanelRect(Ui()->MouseX(), Ui()->MouseY(), 220.0f, CMenus::FriendsCategoryActionsPopupHeight() + CUi::PopupMenuContentInset(), *Ui()->Screen());
			Ui()->DoPopupMenu(&m_FriendsCategoryPopupContext, Panel.x, Panel.y, Panel.w, Panel.h, &m_FriendsCategoryPopupContext, PopupFriendsCategory, FriendsPopupProperties());
		};
		if(Ui()->DoButton_QmIcon(&m_vFriendsCategoryManageButtons[CategoryIndex], EQmIcon::GEAR, FONT_ICON_GEAR, 0, &ManageButton, BUTTONFLAG_LEFT) && !DraggingAnyHeader && !DraggingFriend)
		{
			OpenCategoryManagePopup();
		}
		GameClient()->m_Tooltips.DoToolTip(&m_vFriendsCategoryManageButtons[CategoryIndex], &ManageButton, Localize("Manage categories"));
		GameClient()->m_Tooltips.DoToolTip(&m_vFriendsCategoryExpanded[CategoryIndex], &HeaderAction, Localize("Drag to reorder; right-click or use the gear to manage"));

		const int HeaderResult = Ui()->DoButtonLogic(&m_vFriendsCategoryExpanded[CategoryIndex], 0, &HeaderAction, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
		if(!DraggingAnyHeader && !DraggingFriend && Ui()->MouseButtonClicked(0) && HeaderInside && !Ui()->IsPopupOpen() && Ui()->IsActiveItem(&m_vFriendsCategoryExpanded[CategoryIndex]))
			s_CategoryDragState.Begin(CategoryIndex, Header, vec2(Ui()->MouseX(), Ui()->MouseY()));
		if(!DraggingAnyHeader && HeaderResult == 2 && !DraggingFriend)
		{
			OpenCategoryManagePopup();
		}
		else if(!DraggingAnyHeader && HeaderResult == 1 && !DraggingFriend)
		{
			m_vFriendsCategoryExpanded[CategoryIndex] = !m_vFriendsCategoryExpanded[CategoryIndex];
			SaveFriendsCategoryExpandedState();
		}

		// entries
		if(GroupExpanded)
		{
			// 分组内容用独立游标布局，整体高度固定、按进度只占 VisibleHeight，
			// 行内容随 GroupClip 裁切：展开时逐行露出，收起时滑回标题下方。
			auto FriendRowHeight = [&](const auto &Friend) {
				const bool ShowLastSeen = g_Config.m_QmFriendLastSeen && Friend.FriendState() == IFriends::FRIEND_PLAYER && Friend.ServerInfo() == nullptr;
				return 11.0f + 10.0f + 2 * 2.0f + 1.0f + (Friend.ServerInfo() != nullptr || ShowLastSeen ? 10.0f : 0.0f);
			};
			float GroupHeight = 0.0f;
			for(const auto &Friend : vvFriends[CategoryIndex])
				GroupHeight += SpacingH + FriendRowHeight(Friend);
			CUIRect GroupArea = List;
			const float VisibleHeight = GroupHeight * ExpandT;
			CUIRect GroupClip{List.x, List.y, List.w, maximum(0.0f, VisibleHeight)};
			List.y += VisibleHeight;
			// 滚动内容高度按动画后的可见高度计入，否则折叠过程中内容仍按整组高度估算，
			// 越界钳制会在动画中途把视图瞬间拉回。
			s_ScrollRegion.AddRect(GroupClip);
			if(GroupClip.h > 0.0f)
				Ui()->ClipEnable(&GroupClip);
			for(size_t FriendIndex = 0; FriendIndex < vvFriends[CategoryIndex].size(); ++FriendIndex)
			{
				// space
				{
					CUIRect Space;
					GroupArea.HSplitTop(SpacingH, &Space, &GroupArea);
				}

				CUIRect Rect;
				const auto &Friend = vvFriends[CategoryIndex][FriendIndex];
				const bool IsPlayerFriend = Friend.FriendState() == IFriends::FRIEND_PLAYER;
				const char *pNote = IsPlayerFriend ? GameClient()->Friends()->GetFriendNote(Friend.Name(), Friend.Clan()) : "";
				const bool HasNote = pNote[0] != '\0';
				const bool ShowLastSeen = g_Config.m_QmFriendLastSeen && IsPlayerFriend && Friend.ServerInfo() == nullptr;
				const unsigned NameHash = str_quickhash(Friend.Name());
				const unsigned ClanHash = str_quickhash(Friend.Clan());
				const unsigned AddrHash = Friend.ServerInfo() != nullptr ? str_quickhash(Friend.ServerInfo()->m_aAddress) : 0;
				uintptr_t FriendUiIdBase = ((uintptr_t)NameHash << 32) ^ (uintptr_t)ClanHash ^ ((uintptr_t)AddrHash << 1) ^ (uintptr_t)(Friend.FriendState() << 2);
				if(FriendUiIdBase == 0)
					FriendUiIdBase = 1;
				FriendUiIdBase <<= 4;
				const void *pListItemId = reinterpret_cast<const void *>(FriendUiIdBase | 0x1);
				const void *pRemoveButtonId = reinterpret_cast<const void *>(FriendUiIdBase | 0x3);
				const void *pCopyButtonId = reinterpret_cast<const void *>(FriendUiIdBase | 0x9);
				const void *pFollowButtonId = reinterpret_cast<const void *>(FriendUiIdBase | 0xb);
				const void *pPinButtonId = reinterpret_cast<const void *>(FriendUiIdBase | 0xd);
				const void *pCommunityTooltipId = reinterpret_cast<const void *>(FriendUiIdBase | 0x5);
				const void *pSkinTooltipId = reinterpret_cast<const void *>(FriendUiIdBase | 0x7);
				GroupArea.HSplitTop(FriendRowHeight(Friend), &Rect, &GroupArea);
				if(s_ScrollRegion.RectClipped(Rect) || Rect.y + Rect.h > GroupClip.y + GroupClip.h + 0.5f)
					continue;
				++VisibleFriendItems;

				const SQmFriendLastSeenEntry *pLastSeen = ShowLastSeen ? GameClient()->m_QmFriendLastSeen.Get(Friend.Name()) : nullptr;
				CUIRect FriendRow = Rect;
				const bool Inside = Ui()->MouseHovered(&Rect);
				int ButtonResult = Ui()->DoButtonLogic(pListItemId, 0, &Rect, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);

				if(Friend.ServerInfo() || HasNote || IsPlayerFriend)
				{
					std::string &TooltipText = m_vFriendTooltipText[FriendTooltipIndex];
					TooltipText.clear();
					if(Friend.ServerInfo() && HasNote)
					{
						TooltipText = Localize("Click to select server. Double click to join your friend.");
						TooltipText.append("\n");
						TooltipText.append(Localize("Note:"));
						TooltipText.append(" ");
						TooltipText.append(pNote);
					}
					else if(Friend.ServerInfo())
					{
						TooltipText = Localize("Click to select server. Double click to join your friend.");
					}
					else if(HasNote)
					{
						TooltipText = Localize("Note:");
						TooltipText.append(" ");
						TooltipText.append(pNote);
					}
					if(IsPlayerFriend)
					{
						if(!TooltipText.empty())
							TooltipText.append("\n");
						TooltipText.append(Localize("Drag to another category to move this friend."));
					}
					if(ShowLastSeen)
					{
						if(!TooltipText.empty())
							TooltipText.append("\n");
						TooltipText.append(Localize("DDStats shows UTC dates, updates daily and matches players by name only."));
					}
					// 好友卡片在右栏，提示贴卡片左侧向左弹出，避免盖住卡片本身与右侧动作区。
					GameClient()->m_Tooltips.DoToolTipFromSide(pListItemId, &Rect, TooltipText.c_str(), EQmTooltipPlacement::LEFT, 320.0f);
				}
				++FriendTooltipIndex;
				const bool IsOffline = Friend.ServerInfo() == nullptr;
				const bool FollowingThisFriend = GameClient()->m_PieMenu.IsFollowingPlayer(Friend.Name(), Friend.Clan()) ||
								 (m_FriendAutoFollowState.m_Active && str_comp(m_FriendAutoFollowState.m_aName, Friend.Name()) == 0 && str_comp(m_FriendAutoFollowState.m_aClan, Friend.Clan()) == 0);
				const bool InSelectedServer = m_SelectedIndex >= 0 && Friend.ServerInfo() && Friend.ServerInfo()->m_ServerIndex == ServerBrowser()->SortedGet(m_SelectedIndex)->m_ServerIndex;
				const ColorRGBA Color = PlayerBackgroundColor(Friend.FriendState() == IFriends::FRIEND_PLAYER, Friend.FriendState() == IFriends::FRIEND_CLAN, IsOffline ? true : Friend.IsAfk(), InSelectedServer, Inside);
				// 只看动效档位：该面板在部分渲染通道里 AnimationsBlocked 为真，
				// 带上它会让进场动画整个失效（表现为卡片「闪一下」）。
				const bool FeedbackMotion = g_Config.m_QmUiMotionLevel > 0 && !Ui()->MeasurementOnly();
				CUiV2AnimationRuntime *pAnimRuntime = Ui()->QmAnimationRuntime();
				// 卡片进场淡入：底板颜色直接乘 alpha（DrawRoundedSurface 不过 ControlAlphaScale），
				// 文字与图标走作用域缩放，两者同时从透明过渡到不透明。
				const int64_t CardFirstSeen = s_FriendCardFirstSeen.try_emplace(FriendUiIdBase, FadeNow).first->second;
				const float CardElapsed = std::max(0.0f, (float)(FadeNow - CardFirstSeen) / (float)FadeFreq);
				// 与服务器列表/设置列表同一套入场：淡入 + 临界阻尼上浮，行间错开。
				const SQmListEntryVisual CardVisual = FeedbackMotion ? QmListEntryVisual(CardElapsed, (int)FriendIndex, g_Config.m_QmUiMotionLevel) : SQmListEntryVisual{1.0f, 0.0f};
				const float CardAlpha = CardVisual.m_Fade;
				ColorRGBA CardFill = Color;
				CardFill.a *= CardAlpha;
				const CUiScopedControlAlphaScale CardFade(Ui(), CardAlpha);
				// 上浮归位：绘制矩形整体下移，动画结束后回到布局位置。
				Rect.y += CardVisual.m_OffsetY;
				FriendRow.y += CardVisual.m_OffsetY;
				DrawRoundedSurface(Ui(), Rect, CardFill, ColorRGBA(), ui_token::radius::SMALL_MENU);
				Rect.Margin(2.0f, &Rect);
				const uint64_t FollowTagKey = BuildUiAnimNodeKey(MakeUiScopeHash("browser_friend_follow_tag"), FriendUiIdBase);
				const uint64_t ActionFadeKey = BuildUiAnimNodeKey(MakeUiScopeHash("browser_friend_action_fade"), FriendUiIdBase);
				const float FollowTagTargetWidth = FollowingThisFriend ? FollowTagSlotWidth : 0.0f;
				const float FollowTagTargetAlpha = FollowingThisFriend ? 1.0f : 0.0f;
				float FollowTagCurrentWidth = FollowTagTargetWidth;
				float FollowTagAlpha = FollowTagTargetAlpha;
				if(FeedbackMotion && pAnimRuntime != nullptr)
				{
					FollowTagCurrentWidth = ResolveUiAnimValue(*pAnimRuntime, FollowTagKey, EUiAnimProperty::WIDTH, FollowTagTargetWidth, ui_token::motion::HOVER_FADE.m_DurationSec, ui_token::motion::HOVER_FADE.m_Easing);
					FollowTagAlpha = ResolveUiAnimValue(*pAnimRuntime, FollowTagKey, EUiAnimProperty::ALPHA, FollowTagTargetAlpha, ui_token::motion::HOVER_FADE.m_DurationSec, ui_token::motion::HOVER_FADE.m_Easing);
				}
				FollowTagCurrentWidth = std::clamp(FollowTagCurrentWidth, 0.0f, FollowTagSlotWidth);
				FollowTagAlpha = std::clamp(FollowTagAlpha, 0.0f, 1.0f);
				// 只有跟随时才让标签参与布局；退场期间保留逐渐收窄的槽位，避免瞬间回弹。
				CUIRect FollowSlot{};
				if(FollowTagCurrentWidth > 0.5f)
				{
					Rect.VSplitRight(FollowTagCurrentWidth, &Rect, &FollowSlot);
					Rect.VSplitRight(FollowTagGap, &Rect, nullptr);
				}
				if(FollowTagAlpha > 0.01f)
					DrawFriendFollowRing(Ui(), FriendRow, FollowTagAlpha, FeedbackMotion);
				if(FollowSlot.w > 0.5f && FollowTagAlpha > 0.01f)
				{
					// 胶囊统一由标签组件画：高度/内边距/字号固定，颜色取好友卡片
					// 主色（accent）派生的一档更深色——同一色系、更实，读作「这张卡
					// 上的状态」；右缘让 6 单位，避免边框压到卡片彩虹描边。
					CUIRect FollowTag = FollowSlot;
					FollowTag.w = maximum(0.0f, FollowTag.w - FollowTagRightInset);
					FollowTag = ui_tag::FitRect(FollowTag, minimum(FollowTagWidth, FollowTag.w));
					// 文字等槽位展开到接近满宽再画，避免中途把 "Follow" 压成 "F…"。
					if(FollowTagCurrentWidth >= FollowTagSlotWidth * 0.9f)
					{
						CUiScopedControlAlphaScale FollowFade(Ui(), FollowTagAlpha);
						const ColorRGBA FollowColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiAccentColor));
						ui_tag::DrawInRect(Ui(), FollowTag, Localize("Follow"), ui_tag::EColorMode::DERIVED, FollowColor, nullptr, FollowTagAlpha);
					}
				}

				CUIRect ButtonsRow, PinButton{}, FollowButton, CopyButton, RemoveButton, NameLabel, ClanLabel, InfoLabel;
				Rect.HSplitTop(16.0f, &ButtonsRow, nullptr);
				ButtonsRow.VSplitRight(13.0f, &ButtonsRow, &RemoveButton);
				ButtonsRow.VSplitRight(15.0f, &ButtonsRow, &CopyButton);
				ButtonsRow.VSplitRight(15.0f, &ButtonsRow, &FollowButton);
				if(IsPlayerFriend)
				{
					ButtonsRow.VSplitRight(15.0f, nullptr, &PinButton);
					PinButton.VSplitLeft(2.0f, nullptr, &PinButton);
					PinButton.HMargin((PinButton.h - PinButton.w) / 2.0f, &PinButton);
				}
				FollowButton.VSplitLeft(2.0f, nullptr, &FollowButton);
				CopyButton.VSplitLeft(2.0f, nullptr, &CopyButton);
				FollowButton.HMargin((FollowButton.h - FollowButton.w) / 2.0f, &FollowButton);
				CopyButton.HMargin((CopyButton.h - CopyButton.w) / 2.0f, &CopyButton);
				RemoveButton.HMargin((RemoveButton.h - RemoveButton.w) / 2.0f, &RemoveButton);
				Rect.VSplitLeft(2.0f, nullptr, &Rect);

				float ActionAlpha = Inside ? 1.0f : 0.0f;
				if(FeedbackMotion && pAnimRuntime != nullptr)
				{
					SUiAnimTransition ActionTransition = ui_token::motion::HOVER_FADE;
					ActionTransition.m_DurationSec = ActionFadeInSec;
					ActionTransition.m_DelaySec = Inside ? 0.0f : ActionFadeOutDelaySec;
					ActionTransition.m_Priority = 1;
					ActionTransition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
					ActionAlpha = pAnimRuntime->ResolveTargetValue(ActionFadeKey, EUiAnimProperty::ALPHA, Inside ? 1.0f : 0.0f, ActionTransition);
				}
				ActionAlpha = std::clamp(ActionAlpha, 0.0f, 1.0f);
				const bool ActionVisible = Inside || ActionAlpha > 0.01f;

				if(Friend.ServerInfo() || ShowLastSeen)
					Rect.HSplitBottom(10.0f, &Rect, &InfoLabel);
				Rect.HSplitTop(11.0f + 10.0f, &Rect, nullptr);

				// tee
				CUIRect Skin;
				Rect.VSplitLeft(Rect.h, &Skin, &Rect);
				Rect.VSplitLeft(2.0f, nullptr, &Rect);
				if(Friend.Skin()[0] != '\0')
				{
					const CTeeRenderInfo TeeInfo = GetTeeRenderInfo(vec2(Skin.w, Skin.h), Friend.Skin(), Friend.CustomSkinColors(), Friend.CustomSkinColorBody(), Friend.CustomSkinColorFeet());
					RenderTools()->RenderIdleTeeAtCenter(&TeeInfo, vec2(Skin.x + Skin.w / 2.0f, Skin.y + Skin.h * 0.55f), Friend.IsAfk() ? EMOTE_BLINK : EMOTE_NORMAL);
					Ui()->DoButtonLogic(pSkinTooltipId, 0, &Skin, BUTTONFLAG_NONE);
					GameClient()->m_Tooltips.DoToolTip(pSkinTooltipId, &Skin, Friend.Skin());
				}
				else if(Friend.Skin7(protocol7::SKINPART_BODY)[0] != '\0')
				{
					const CTeeRenderInfo TeeInfo = GetTeeRenderInfo(vec2(Skin.w, Skin.h), "default", false, 0, 0);
					RenderTools()->RenderIdleTeeAtCenter(&TeeInfo, vec2(Skin.x + Skin.w / 2.0f, Skin.y + Skin.h * 0.55f), Friend.IsAfk() ? EMOTE_BLINK : EMOTE_NORMAL);
				}
				Rect.HSplitTop(11.0f, &NameLabel, &ClanLabel);

				SLabelProperties FriendLabelProps;
				// 名字截断到动作区左缘；跟随标签出现时布局随动画槽位收窄，退场后恢复整行宽度。
				FriendLabelProps.m_MaxWidth = maximum(0.0f, (IsPlayerFriend ? PinButton.x : FollowButton.x) - NameLabel.x - 2.0f);
				FriendLabelProps.m_DisallowNewline = true;
				FriendLabelProps.m_EllipsisAtEnd = true;

				// name
				Ui()->DoLabel(&NameLabel, Friend.Name(), FontSize - 1.0f, TEXTALIGN_ML, FriendLabelProps);

				// clan
				Ui()->DoLabel(&ClanLabel, Friend.Clan(), FontSize - 2.0f, TEXTALIGN_ML, FriendLabelProps);

				// server info
				if(Friend.ServerInfo())
				{
					// community icon
					const CCommunity *pCommunity = ServerBrowser()->Community(Friend.ServerInfo()->m_aCommunityId);
					if(pCommunity != nullptr)
					{
						const CCommunityIcon *pIcon = m_CommunityIcons.Find(pCommunity->Id());
						if(pIcon != nullptr)
						{
							CUIRect CommunityIcon;
							InfoLabel.VSplitLeft(21.0f, &CommunityIcon, &InfoLabel);
							InfoLabel.VSplitLeft(2.0f, nullptr, &InfoLabel);
							m_CommunityIcons.Render(pIcon, CommunityIcon, true);
							Ui()->DoButtonLogic(pCommunityTooltipId, 0, &CommunityIcon, BUTTONFLAG_NONE);
							GameClient()->m_Tooltips.DoToolTip(pCommunityTooltipId, &CommunityIcon, pCommunity->Name());
						}
					}

					// server info text
					char aLatency[16];
					CServerInfo::FormatLatency(aLatency, Friend.ServerInfo()->m_Latency);
					if(aLatency[0] != '\0')
						str_format(aBuf, sizeof(aBuf), "%s | %s | %s", Friend.ServerInfo()->m_aMap, Friend.ServerInfo()->m_aGameType, aLatency);
					else
						str_format(aBuf, sizeof(aBuf), "%s | %s", Friend.ServerInfo()->m_aMap, Friend.ServerInfo()->m_aGameType);
					Ui()->DoLabel(&InfoLabel, aBuf, FontSize - 2.0f, TEXTALIGN_ML);
				}
				else if(ShowLastSeen)
				{
					const char *pValue = Localize("No record");
					if(pLastSeen)
					{
						if(!pLastSeen->m_Date.empty())
							pValue = pLastSeen->m_Date.c_str();
						else if(pLastSeen->m_Status == EQmFriendLastSeenStatus::LOADING)
							pValue = Localize("Querying...");
						else if(pLastSeen->m_Status == EQmFriendLastSeenStatus::ERROR)
							pValue = Localize("Unavailable");
					}
					str_format(aBuf, sizeof(aBuf), Localize("Last seen (DDStats): %s"), pValue);
					SLabelProperties LastSeenProps;
					LastSeenProps.m_MaxWidth = maximum(0.0f, InfoLabel.w);
					LastSeenProps.m_DisallowNewline = true;
					LastSeenProps.m_EllipsisAtEnd = true;
					Ui()->DoLabel(&InfoLabel, aBuf, FontSize - 2.0f, TEXTALIGN_ML, LastSeenProps);
				}

				// 置顶入口：已置顶时常显，否则和右侧三个动作图标一样只在悬停时出现，
				// 避免每行都顶一个常驻箭头。重排在下一次列表刷新时生效。
				// 图标用 Phosphor arrow-line-up（箭头 + 上方一横），不是 bookmark 或普通上箭头。
				const bool PinVisible = IsPlayerFriend && (Friend.IsPinned() || ActionVisible);
				if(PinVisible)
				{
					const IUiContext Context = ui_widget::ControlContext(Ui());
					CUiScopedControlAlphaScale PinFade(Ui(), Friend.IsPinned() ? 1.0f : ActionAlpha);
					const CUIRect PinHit = FriendActionHitRect(PinButton);
					if(ui_widget::DoIconButton(Context, pPinButtonId, EQmIcon::ARROW_LINE_UP, FONT_ICON_ARROW_LINE_UP, Friend.IsPinned() ? 1 : 0, PinButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Inside || Friend.IsPinned(), std::nullopt, false, true, &PinHit) != 0)
					{
						GameClient()->Friends()->SetFriendPinned(Friend.Name(), Friend.Clan(), !Friend.IsPinned());
						ButtonResult = 0;
					}
					if(Inside || Friend.IsPinned())
						GameClient()->m_Tooltips.DoToolTip(pPinButtonId, &PinButton, Friend.IsPinned() ? Localize("Unpin friend") : Localize("Pin friend"));
				}

				// remove button：动作图标常驻绘制但只有悬停时接收输入；离开行后
				// 先保留 150ms 再淡出，斜穿列表时不会一闪一闪。
				if(ActionVisible)
				{
					const IUiContext Context = ui_widget::ControlContext(Ui());
					CUiScopedControlAlphaScale ActionFade(Ui(), ActionAlpha);
					const CUIRect FollowHit = FriendActionHitRect(FollowButton);
					const CUIRect CopyHit = FriendActionHitRect(CopyButton);
					const CUIRect RemoveHit = FriendActionHitRect(RemoveButton);
					// 关注 ⇄ 取消关注是同位置双态：play ↔ stop 形变帧条，Fill 字重回交叉淡化。
					IUiContext FollowCtx = ui_widget::ControlContext(Ui());
					ui_widget::SIconSwapBinding FollowBinding = ui_widget::ResolveIconSwapBinding(
						FollowCtx, EQmIconSwapPair::PLAY_STOP,
						EQmIcon::PLAY, FONT_ICON_PLAY,
						EQmIcon::STOP, FONT_ICON_STOP,
						FollowingThisFriend ? 1.0f : 0.0f, str_quickhash("friend-follow-icon"), pFollowButtonId, 0.24f, true);
					ui_widget::SIconButtonIconDraw FollowDraw{&ui_widget::DrawIconSwapBinding, &FollowBinding};
					const bool FollowPressed = Friend.ServerInfo() && ui_widget::DoIconButton(Context, pFollowButtonId, EQmIcon::COUNT, nullptr, FollowingThisFriend ? 1 : 0, FollowButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Inside, std::nullopt, false, true, &FollowHit, &FollowDraw) != 0;
					const bool CopyPressed = ui_widget::DoIconButton(Context, pCopyButtonId, EQmIcon::COPY, FONT_ICON_COPY, 0, CopyButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Inside, std::nullopt, false, true, &CopyHit) != 0;
					const bool RemovePressed = ui_widget::DoIconButton(Context, pRemoveButtonId, EQmIcon::TRASH, FONT_ICON_TRASH, 0, RemoveButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Inside, std::nullopt, false, true, &RemoveHit) != 0;
					if(Inside)
					{
						if(CopyPressed)
						{
							Input()->SetClipboardText(Friend.Name()[0] != '\0' ? Friend.Name() : Friend.Clan());
							ButtonResult = 0;
						}
						GameClient()->m_Tooltips.DoToolTip(pCopyButtonId, &CopyButton, Friend.FriendState() == IFriends::FRIEND_PLAYER ? Localize("Click to copy this player's name to clipboard") : Localize("Click to copy this clan's name to clipboard"));
						if(FollowPressed)
						{
							if(FollowingThisFriend)
							{
								GameClient()->m_PieMenu.CancelFollow();
								StopFriendAutoFollow(m_FriendAutoFollowState);
							}
							else
							{
								GameClient()->m_PieMenu.CancelFollow();
								StartFriendAutoFollow(m_FriendAutoFollowState, Friend.Name(), Friend.Clan(), Friend.ServerInfo()->m_aAddress);
							}
							ButtonResult = 0;
						}
						if(Friend.ServerInfo())
							GameClient()->m_Tooltips.DoToolTip(pFollowButtonId, &FollowButton, FollowingThisFriend ? Localize("Stop following this friend", "Friend auto follow") : Localize("Follow this friend across servers", "Friend auto follow"));
						if(RemovePressed)
						{
							str_copy(m_aRemoveFriendName, Friend.Name(), sizeof(m_aRemoveFriendName));
							str_copy(m_aRemoveFriendClan, Friend.Clan(), sizeof(m_aRemoveFriendClan));
							m_RemoveFriendState = Friend.FriendState();
							m_HasRemoveFriend = true;
							OpenRemovePopup = true;
							ButtonResult = 0;
						}
						GameClient()->m_Tooltips.DoToolTip(pRemoveButtonId, &RemoveButton, Friend.FriendState() == IFriends::FRIEND_PLAYER ? Localize("Click to remove this player from your friends list.") : Localize("Click to remove this clan from your friends list."));
					}
				}

				const auto HoverFriendAction = [&](const CUIRect &Icon) {
					const CUIRect Hit = FriendActionHitRect(Icon);
					return Ui()->MouseHovered(&Hit);
				};
				const bool InsideFriendAction = (IsPlayerFriend && Ui()->MouseHovered(&PinButton)) || HoverFriendAction(CopyButton) || HoverFriendAction(RemoveButton) || (Friend.ServerInfo() && HoverFriendAction(FollowButton));
				if(IsPlayerFriend && Ui()->MouseButtonClicked(0) && Inside && !InsideFriendAction && !Ui()->IsPopupOpen() && (Ui()->IsActiveItem(pListItemId) || Ui()->ActiveItem() == nullptr))
				{
					s_FriendDragState.Begin(pListItemId, Friend.FriendState(), Friend.Name(), Friend.Clan(), GameClient()->Friends()->GetFriendCategory(Friend.Name(), Friend.Clan()), FriendRow, vec2(Ui()->MouseX(), Ui()->MouseY()));
					if(Ui()->ActiveItem() == nullptr)
						Ui()->SetActiveItem(&s_FriendDragState);
				}

				if(ButtonResult == 2)
				{
					const bool CanMoveCategory = Friend.FriendState() == IFriends::FRIEND_PLAYER && !IsClanMembersCategory(Friend.Category());
					const bool CanEditNote = Friend.FriendState() == IFriends::FRIEND_PLAYER;

					m_FriendsActionPopupContext.Reset();
					m_FriendsActionPopupContext.m_pScrollRegion = &s_FriendsActionPopupScrollRegion;
					m_vFriendsActionEntries.clear();
					StyleFriendsSelectionPopup(m_FriendsActionPopupContext);

					if(CanMoveCategory)
					{
						m_FriendsActionPopupContext.m_vEntries.emplace_back(Localize("Move to category"));
						m_vFriendsActionEntries.push_back(FRIEND_ACTION_MOVE_CATEGORY);
					}

					if(CanEditNote)
					{
						m_FriendsActionPopupContext.m_vEntries.emplace_back(Localize("Edit note"));
						m_vFriendsActionEntries.push_back(FRIEND_ACTION_EDIT_NOTE);
						if(HasNote)
						{
							m_FriendsActionPopupContext.m_vEntries.emplace_back(Localize("Clear note"));
							m_vFriendsActionEntries.push_back(FRIEND_ACTION_CLEAR_NOTE);
						}
					}

					if(Friend.ServerInfo())
					{
						m_FriendsActionPopupContext.m_vEntries.emplace_back(FollowingThisFriend ? Localize("Stop following this friend", "Friend auto follow") : Localize("Follow this friend across servers", "Friend auto follow"));
						m_vFriendsActionEntries.push_back(FollowingThisFriend ? FRIEND_ACTION_STOP_FOLLOW : FRIEND_ACTION_FOLLOW);
					}

					m_FriendsActionPopupContext.m_vEntries.emplace_back(Localize("Remove friend"));
					m_vFriendsActionEntries.push_back(FRIEND_ACTION_REMOVE);

					if(!m_FriendsActionPopupContext.m_vEntries.empty())
					{
						str_copy(m_aFriendActionName, Friend.Name(), sizeof(m_aFriendActionName));
						str_copy(m_aFriendActionClan, Friend.Clan(), sizeof(m_aFriendActionClan));
						str_copy(m_aFriendActionAddress, Friend.ServerInfo() != nullptr ? Friend.ServerInfo()->m_aAddress : "", sizeof(m_aFriendActionAddress));
						m_FriendActionState = Friend.FriendState();
						m_HasFriendAction = true;
						Ui()->ShowPopupSelection(Ui()->MouseX(), Ui()->MouseY(), &m_FriendsActionPopupContext);
					}

					ButtonResult = 0;
				}

				// handle click and double click on item
				if(ButtonResult == 1 && Friend.ServerInfo())
				{
					str_copy(g_Config.m_UiServerAddress, Friend.ServerInfo()->m_aAddress);
					m_ServerBrowserShouldRevealSelection = true;
					if(Ui()->DoDoubleClickLogic(pListItemId))
					{
						Connect(g_Config.m_UiServerAddress);
					}
				}
			}

			if(GameClient()->Friends()->NumFriends() == 0 && CategoryIndex == 0)
			{
				const char *pText = Localize("Add friends by entering their name below or by clicking their name in the player list.");
				const float DescriptionMargin = 2.0f;
				const STextBoundingBox BoundingBox = TextRender()->TextBoundingBox(FontSize, pText, -1, List.w - 2 * DescriptionMargin);
				CUIRect EmptyDescription;
				List.HSplitTop(BoundingBox.m_H + 2 * DescriptionMargin, &EmptyDescription, &List);
				s_ScrollRegion.AddRect(EmptyDescription);
				EmptyDescription.Margin(DescriptionMargin, &EmptyDescription);
				SLabelProperties DescriptionProps;
				DescriptionProps.m_MaxWidth = EmptyDescription.w;
				Ui()->DoLabel(&EmptyDescription, pText, FontSize, TEXTALIGN_ML, DescriptionProps);
			}

			if(GroupClip.h > 0.0f)
				Ui()->ClipDisable();
		}

		// space
		{
			CUIRect Space;
			List.HSplitTop(SpacingH, &Space, &List);
			s_ScrollRegion.AddRect(Space);
		}
	}

	auto UpdateCategoryDropPreview = [&]() {
		s_CategoryDropPreview = SFriendsCategoryDropPreview();

		if(s_CategoryDragState.m_DraggingIndex < 0 || NumCategories <= 1)
			return;

		if(!Ui()->MouseHovered(&ListViewport))
			return;

		std::vector<const SFriendsCategoryHeaderInfo *> vFilteredHeaders;
		vFilteredHeaders.reserve(vCategoryHeaders.size());
		for(const auto &HeaderInfo : vCategoryHeaders)
		{
			if(HeaderInfo.m_CategoryIndex != s_CategoryDragState.m_DraggingIndex)
				vFilteredHeaders.push_back(&HeaderInfo);
		}

		int InsertIndex = 0;
		const float MouseY = Ui()->MouseY();
		for(const auto *pHeaderInfo : vFilteredHeaders)
		{
			const float MidY = pHeaderInfo->m_Rect.y + pHeaderInfo->m_Rect.h * 0.5f;
			if(MouseY > MidY)
				++InsertIndex;
		}

		float LineY = ListViewport.y + ms_ListheaderHeight * 0.5f;
		if(!vFilteredHeaders.empty())
		{
			if(InsertIndex <= 0)
			{
				LineY = maximum(ListViewport.y, vFilteredHeaders.front()->m_Rect.y - SpacingH * 0.5f);
			}
			else if(InsertIndex >= (int)vFilteredHeaders.size())
			{
				const SFriendsCategoryHeaderInfo *pLast = vFilteredHeaders.back();
				LineY = pLast->m_Rect.y + pLast->m_Rect.h + SpacingH * 0.5f;
			}
			else
			{
				const SFriendsCategoryHeaderInfo *pPrev = vFilteredHeaders[InsertIndex - 1];
				const SFriendsCategoryHeaderInfo *pNext = vFilteredHeaders[InsertIndex];
				LineY = (pPrev->m_Rect.y + pPrev->m_Rect.h + pNext->m_Rect.y) * 0.5f;
			}
		}

		const float LinePadding = std::clamp(6.0f * UiScale, 3.0f, 8.0f);
		CUIRect LineRect;
		LineRect.x = ListViewport.x + LinePadding;
		LineRect.w = maximum(0.0f, ListViewport.w - LinePadding * 2.0f);
		LineRect.y = LineY - CategoryDropPreviewThickness * 0.5f;
		LineRect.h = CategoryDropPreviewThickness;

		s_CategoryDropPreview.m_Active = true;
		s_CategoryDropPreview.m_Valid = true;
		s_CategoryDropPreview.m_DraggedIndex = s_CategoryDragState.m_DraggingIndex;
		s_CategoryDropPreview.m_InsertIndex = InsertIndex;
		s_CategoryDropPreview.m_LineRect = LineRect;
	};

	auto RenderCategoryDragGhost = [&]() {
		if(s_CategoryDragState.m_DraggingIndex < 0 || !s_CategoryDragState.m_HasDragRect)
			return;

		CUIRect Ghost;
		Ghost.x = Ui()->MouseX() - s_CategoryDragState.m_GrabOffset.x;
		Ghost.y = Ui()->MouseY() - s_CategoryDragState.m_GrabOffset.y;
		Ghost.w = s_CategoryDragState.m_DraggedWidth;
		Ghost.h = s_CategoryDragState.m_DraggedHeight;

		CUIRect Shadow = Ghost;
		Shadow.x += 1.5f;
		Shadow.y += 2.0f;
		Shadow.Draw(BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.45f)), IGraphics::CORNER_ALL, 5.0f);

		Ghost.Draw(CategoryDragGhostColor, IGraphics::CORNER_ALL, 5.0f);
		DrawCategoryDragOutline(Ghost);
	};

	auto CommitCategoryDropPreview = [&]() {
		if(!s_CategoryDropPreview.m_Active || !s_CategoryDropPreview.m_Valid || s_CategoryDropPreview.m_DraggedIndex < 0)
			return false;

		const int FromIndex = s_CategoryDropPreview.m_DraggedIndex;
		const int ToIndex = std::clamp(s_CategoryDropPreview.m_InsertIndex, 0, NumCategories - 1);
		if(FromIndex == ToIndex)
			return false;

		char aSelectedCategory[IFriends::MAX_FRIEND_CATEGORY_LENGTH] = {};
		const bool HasSelectedCategory = m_FriendAddCategoryIndex >= 0 && m_FriendAddCategoryIndex < NumCategories;
		if(HasSelectedCategory)
			str_copy(aSelectedCategory, GameClient()->Friends()->GetCategory(m_FriendAddCategoryIndex), sizeof(aSelectedCategory));

		if(!GameClient()->Friends()->MoveCategory(FromIndex, ToIndex))
			return false;

		MoveCategoryExpandedState(FromIndex, ToIndex);
		SaveFriendsCategoryExpandedState();
		FriendlistOnUpdate();

		if(HasSelectedCategory)
		{
			const int SelectedCategoryIndex = GameClient()->Friends()->FindCategory(aSelectedCategory);
			if(SelectedCategoryIndex >= 0)
				m_FriendAddCategoryIndex = SelectedCategoryIndex;
		}

		return true;
	};

	if(s_CategoryDragState.m_DraggingIndex >= 0)
	{
		s_ScrollRegion.DoEdgeScrolling();
		UpdateCategoryDropPreview();
		if(s_CategoryDropPreview.m_Active && s_CategoryDropPreview.m_Valid)
			s_CategoryDropPreview.m_LineRect.Draw(CategoryDropPreviewColor, IGraphics::CORNER_ALL, CategoryDropPreviewThickness);
		RenderCategoryDragGhost();
	}

	const bool MouseReleased = !Ui()->MouseButton(0) && Ui()->LastMouseButton(0);
	if(DraggingFriend)
	{
		s_ScrollRegion.DoEdgeScrolling();
		CUIRect Ghost{Ui()->MouseX() - s_FriendDragState.m_GrabOffset.x, Ui()->MouseY() - s_FriendDragState.m_GrabOffset.y, s_FriendDragState.m_DraggedWidth, s_FriendDragState.m_DraggedHeight};
		Ghost.Draw(CategoryDragGhostColor, IGraphics::CORNER_ALL, 5.0f);
		DrawCategoryDragOutline(Ghost);
		CUIRect NameLabel, TargetLabel;
		Ghost.Margin(3.0f, &Ghost);
		Ghost.HSplitTop(FontSize + 1.0f, &NameLabel, &TargetLabel);
		SLabelProperties GhostLabelProps;
		GhostLabelProps.m_MaxWidth = Ghost.w;
		Ui()->DoLabel(&NameLabel, s_FriendDragState.m_aName, FontSize, TEXTALIGN_ML, GhostLabelProps);
		Ui()->DoLabel(&TargetLabel, FriendDropCategoryIndex >= 0 ? LocalizeFriendsCategory(GameClient()->Friends()->GetCategory(FriendDropCategoryIndex)) : Localize("Move to category"), FontSize - 1.0f, TEXTALIGN_ML, GhostLabelProps);

		if(MouseReleased && FriendDropCategoryIndex >= 0)
		{
			const char *pCategory = GameClient()->Friends()->GetCategory(FriendDropCategoryIndex);
			if(GameClient()->Friends()->SetFriendCategory(s_FriendDragState.m_aName, s_FriendDragState.m_aClan, pCategory))
			{
				m_FriendAddCategoryIndex = FriendDropCategoryIndex;
				FriendlistOnUpdate();
				Client()->ServerBrowserUpdate();
			}
		}
	}
	if(!Ui()->MouseButton(0))
	{
		if(MouseReleased && s_CategoryDragState.m_DraggingIndex >= 0)
			CommitCategoryDropPreview();
		ResetCategoryDragState();
		ResetFriendDragState();
	}

	s_ScrollRegion.End();
	const bool FriendsScrollActive = QmMenuUiScrollPerfActive(s_ScrollRegion.WheelConsumedThisFrame(), s_ScrollRegion.Active(), s_ScrollRegion.Animating());
	if(FriendsScrollActive)
	{
		StartSettingsPerfScrollWindow("friends_scroll", SettingsPerfContextName(), "friends", "none");
		SQmMenuUiFramePerf MenuUiPerf;
		MenuUiPerf.m_pPage = "friends";
		MenuUiPerf.m_pOperation = "friends_scroll";
		MenuUiPerf.m_ItemsTotal = TotalFriendItems;
		MenuUiPerf.m_ItemsVisible = VisibleFriendItems;
		MenuUiPerf.m_ItemsProcessed = VisibleFriendItems;
		MenuUiPerf.m_ItemsSkipped = maximum(0, TotalFriendItems - VisibleFriendItems);
		MenuUiPerf.m_UiMs = MenuUiPerfEnabled ? (float)std::chrono::duration<double, std::milli>(time_get_nanoseconds() - MenuUiStartTime).count() : -1.0f;
		QmLogMenuUiFramePerf(MenuUiPerf, Client());
	}

	if(m_HasFriendAction && m_FriendsActionPopupContext.m_pSelection != nullptr)
	{
		const int SelectionIndex = m_FriendsActionPopupContext.m_SelectionIndex;
		if(SelectionIndex >= 0 && SelectionIndex < (int)m_vFriendsActionEntries.size())
		{
			const EFriendAction Action = m_vFriendsActionEntries[SelectionIndex];
			if(Action == FRIEND_ACTION_MOVE_CATEGORY)
			{
				m_FriendsMoveCategoryPopupContext.Reset();
				m_FriendsMoveCategoryPopupContext.m_pScrollRegion = &s_FriendsMoveCategoryPopupScrollRegion;
				str_copy(m_FriendsMoveCategoryPopupContext.m_aMessage, Localize("Move to category"));
				StyleFriendsSelectionPopup(m_FriendsMoveCategoryPopupContext);
				for(int MoveCategoryIndex = 0; MoveCategoryIndex < NumCategories; ++MoveCategoryIndex)
				{
					const char *pMoveCategory = GameClient()->Friends()->GetCategory(MoveCategoryIndex);
					if(IsClanMembersCategory(pMoveCategory))
						continue;
					m_FriendsMoveCategoryPopupContext.m_vEntries.emplace_back(pMoveCategory);
				}

				if(!m_FriendsMoveCategoryPopupContext.m_vEntries.empty())
				{
					str_copy(m_aMoveCategoryFriendName, m_aFriendActionName, sizeof(m_aMoveCategoryFriendName));
					str_copy(m_aMoveCategoryFriendClan, m_aFriendActionClan, sizeof(m_aMoveCategoryFriendClan));
					m_HasMoveCategoryFriend = true;
					Ui()->ShowPopupSelection(Ui()->MouseX(), Ui()->MouseY(), &m_FriendsMoveCategoryPopupContext);
				}
			}
			else if(Action == FRIEND_ACTION_EDIT_NOTE)
			{
				m_FriendNotePopupContext.m_pMenus = this;
				str_copy(m_FriendNotePopupContext.m_aName, m_aFriendActionName, sizeof(m_FriendNotePopupContext.m_aName));
				str_copy(m_FriendNotePopupContext.m_aClan, m_aFriendActionClan, sizeof(m_FriendNotePopupContext.m_aClan));
				m_FriendNotePopupContext.m_NoteInput.Set(GameClient()->Friends()->GetFriendNote(m_aFriendActionName, m_aFriendActionClan));
				m_FriendNotePopupContext.m_NoteInput.SelectAll();
				Ui()->DoPopupMenu(&m_FriendNotePopupContext, Ui()->MouseX(), Ui()->MouseY(), 220.0f, 70.0f + CUi::PopupMenuContentInset(), &m_FriendNotePopupContext, PopupFriendNote, FriendsPopupProperties());
			}
			else if(Action == FRIEND_ACTION_CLEAR_NOTE)
			{
				GameClient()->Friends()->ClearFriendNote(m_aFriendActionName, m_aFriendActionClan);
			}
			else if(Action == FRIEND_ACTION_FOLLOW)
			{
				GameClient()->m_PieMenu.CancelFollow();
				StartFriendAutoFollow(m_FriendAutoFollowState, m_aFriendActionName, m_aFriendActionClan, m_aFriendActionAddress);
			}
			else if(Action == FRIEND_ACTION_STOP_FOLLOW)
			{
				GameClient()->m_PieMenu.CancelFollow();
				StopFriendAutoFollow(m_FriendAutoFollowState);
			}
			else if(Action == FRIEND_ACTION_REMOVE)
			{
				str_copy(m_aRemoveFriendName, m_aFriendActionName, sizeof(m_aRemoveFriendName));
				str_copy(m_aRemoveFriendClan, m_aFriendActionClan, sizeof(m_aRemoveFriendClan));
				m_RemoveFriendState = m_FriendActionState;
				m_HasRemoveFriend = true;
				OpenRemovePopup = true;
			}
		}

		m_FriendsActionPopupContext.Reset();
		m_vFriendsActionEntries.clear();
		m_HasFriendAction = false;
		m_aFriendActionName[0] = '\0';
		m_aFriendActionClan[0] = '\0';
		m_aFriendActionAddress[0] = '\0';
		m_FriendActionState = IFriends::FRIEND_NO;
	}
	else if(m_HasFriendAction && !Ui()->IsPopupOpen(&m_FriendsActionPopupContext))
	{
		m_FriendsActionPopupContext.Reset();
		m_vFriendsActionEntries.clear();
		m_HasFriendAction = false;
		m_aFriendActionName[0] = '\0';
		m_aFriendActionClan[0] = '\0';
		m_aFriendActionAddress[0] = '\0';
		m_FriendActionState = IFriends::FRIEND_NO;
	}

	if(m_HasMoveCategoryFriend && m_FriendsMoveCategoryPopupContext.m_pSelection != nullptr)
	{
		const char *pCategory = m_FriendsMoveCategoryPopupContext.m_pSelection->c_str();

		if(pCategory != nullptr && GameClient()->Friends()->SetFriendCategory(m_aMoveCategoryFriendName, m_aMoveCategoryFriendClan, pCategory))
		{
			m_FriendAddCategoryIndex = maximum(0, GameClient()->Friends()->FindCategory(pCategory));
			FriendlistOnUpdate();
			Client()->ServerBrowserUpdate();
		}

		m_FriendsMoveCategoryPopupContext.Reset();
		m_HasMoveCategoryFriend = false;
		m_aMoveCategoryFriendName[0] = '\0';
		m_aMoveCategoryFriendClan[0] = '\0';
	}
	else if(m_HasMoveCategoryFriend && !Ui()->IsPopupOpen(&m_FriendsMoveCategoryPopupContext))
	{
		m_HasMoveCategoryFriend = false;
		m_aMoveCategoryFriendName[0] = '\0';
		m_aMoveCategoryFriendClan[0] = '\0';
	}

	if(OpenRemovePopup && m_HasRemoveFriend)
	{
		char aMessage[256];
		str_format(aMessage, sizeof(aMessage),
			m_RemoveFriendState == IFriends::FRIEND_PLAYER ? Localize("Are you sure that you want to remove the player '%s' from your friends list?") : Localize("Are you sure that you want to remove the clan '%s' from your friends list?"),
			m_RemoveFriendState == IFriends::FRIEND_PLAYER ? m_aRemoveFriendName : m_aRemoveFriendClan);
		// 删除好友是破坏性操作：Cancel/OK + 危险色，正文带具体名字。
		s_RemoveFriendConfirm.Reset();
		s_RemoveFriendConfirm.DefaultButtons();
		s_RemoveFriendConfirm.m_Tone = CUi::SConfirmPopupContext::ETone::DANGER;
		str_copy(s_RemoveFriendConfirm.m_aMessage, aMessage, sizeof(s_RemoveFriendConfirm.m_aMessage));
		Ui()->ShowPopupConfirm(Ui()->MouseX(), Ui()->MouseY(), &s_RemoveFriendConfirm);
		s_RemoveFriendPending = true;
	}
	// 结果下一帧轮询：确认后才删；取消或被页面切换收掉时走原来的取消回调清状态。
	if(s_RemoveFriendPending)
	{
		if(s_RemoveFriendConfirm.m_Result == CUi::SConfirmPopupContext::CONFIRMED)
		{
			s_RemoveFriendPending = false;
			PopupConfirmRemoveFriend();
		}
		else if(s_RemoveFriendConfirm.m_Result == CUi::SConfirmPopupContext::CANCELED || !Ui()->IsPopupOpen(&s_RemoveFriendConfirm))
		{
			s_RemoveFriendPending = false;
			PopupCancelRemoveFriend();
		}
	}

	// add friend
	if(GameClient()->Friends()->NumFriends() < IFriends::MAX_FRIENDS)
	{
		CUIRect Button;
		ServerFriends.Margin(5.0f, &ServerFriends);

		ServerFriends.HSplitTop(18.0f, &Button, &ServerFriends);
		str_format(aBuf, sizeof(aBuf), "%s:", Localize("Name"));
		Ui()->DoLabel(&Button, aBuf, FontSize + 2.0f, TEXTALIGN_ML);
		Button.VSplitLeft(80.0f, nullptr, &Button);
		static CLineInputBuffered<MAX_NAME_LENGTH> s_NameInput;
		const IUiContext ServerBrowserAddFriendTextInputCtx = SettingsUiContext("server_browser_add_friend_text_inputs");
		ui_widget::SInputFieldOptions AddFriendInputOptions;
		AddFriendInputOptions.m_FontSize = FontSize + 2.0f;
		ui_widget::InputField(ServerBrowserAddFriendTextInputCtx, &s_NameInput, Button, AddFriendInputOptions);

		ServerFriends.HSplitTop(3.0f, nullptr, &ServerFriends);
		ServerFriends.HSplitTop(18.0f, &Button, &ServerFriends);
		str_format(aBuf, sizeof(aBuf), "%s:", Localize("Clan"));
		Ui()->DoLabel(&Button, aBuf, FontSize + 2.0f, TEXTALIGN_ML);
		Button.VSplitLeft(80.0f, nullptr, &Button);
		static CLineInputBuffered<MAX_CLAN_LENGTH> s_ClanInput;
		ui_widget::InputField(ServerBrowserAddFriendTextInputCtx, &s_ClanInput, Button, AddFriendInputOptions);

		ServerFriends.HSplitTop(3.0f, nullptr, &ServerFriends);
		ServerFriends.HSplitTop(18.0f, &Button, &ServerFriends);
		str_format(aBuf, sizeof(aBuf), "%s:", Localize("Add to category"));
		Ui()->DoLabel(&Button, aBuf, FontSize + 2.0f, TEXTALIGN_ML);
		Button.VSplitLeft(80.0f, nullptr, &Button);
		std::vector<std::string> vDisplayedCategories;
		std::vector<const char *> vpCategories;
		std::vector<int> vCategoryIndices;
		vDisplayedCategories.reserve(NumCategories);
		vpCategories.reserve(NumCategories);
		vCategoryIndices.reserve(NumCategories);
		for(int CategoryIndex = 0; CategoryIndex < NumCategories; ++CategoryIndex)
		{
			const char *pCategory = GameClient()->Friends()->GetCategory(CategoryIndex);
			if(IsClanMembersCategory(pCategory))
				continue;
			vDisplayedCategories.emplace_back(LocalizeFriendsCategory(pCategory));
			vpCategories.push_back(vDisplayedCategories.back().c_str());
			vCategoryIndices.push_back(CategoryIndex);
		}

		if(vCategoryIndices.empty())
		{
			const int DefaultCategoryIndex = maximum(0, GameClient()->Friends()->FindCategory(GameClient()->Friends()->DefaultCategory()));
			vCategoryIndices.push_back(DefaultCategoryIndex);
			vDisplayedCategories.emplace_back(LocalizeFriendsCategory(GameClient()->Friends()->GetCategory(DefaultCategoryIndex)));
			vpCategories.push_back(vDisplayedCategories.back().c_str());
		}

		int DropDownSelection = 0;
		for(int SelectionIndex = 0; SelectionIndex < (int)vCategoryIndices.size(); ++SelectionIndex)
		{
			if(vCategoryIndices[SelectionIndex] == m_FriendAddCategoryIndex)
			{
				DropDownSelection = SelectionIndex;
				break;
			}
		}

		if(m_FriendAddCategoryIndex < 0 || m_FriendAddCategoryIndex >= NumCategories || IsClanMembersCategory(GameClient()->Friends()->GetCategory(m_FriendAddCategoryIndex)))
			m_FriendAddCategoryIndex = vCategoryIndices[DropDownSelection];
		if(!vpCategories.empty())
		{
			static CScrollRegion s_FriendsCategoryDropDownScrollRegion;
			m_FriendsAddCategoryDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_FriendsCategoryDropDownScrollRegion;
			CUIRect CategoryDropDown, CreateCategoryButton;
			Button.VSplitRight(Button.h, &CategoryDropDown, &CreateCategoryButton);
			CategoryDropDown.VSplitRight(3.0f, &CategoryDropDown, nullptr);
			DropDownSelection = Ui()->DoDropDown(&CategoryDropDown, DropDownSelection, vpCategories.data(), (int)vpCategories.size(), m_FriendsAddCategoryDropDownState);
			if(DropDownSelection >= 0 && DropDownSelection < (int)vCategoryIndices.size())
				m_FriendAddCategoryIndex = vCategoryIndices[DropDownSelection];
			if(Ui()->DoButton_QmIcon(&m_FriendsAddCategoryCreateButton, EQmIcon::PLUS, FONT_ICON_PLUS, 0, &CreateCategoryButton, BUTTONFLAG_LEFT))
			{
				m_FriendsCategoryPopupContext.m_pMenus = this;
				m_FriendsCategoryPopupContext.m_CategoryIndex = m_FriendAddCategoryIndex;
				m_FriendsCategoryPopupContext.m_Mode = CFriendsCategoryPopupContext::MODE_ADD;
				m_FriendsCategoryPopupContext.m_NameInput.Clear();
				const CUIRect Panel = CMenus::SecondaryPanelRect(Ui()->MouseX(), Ui()->MouseY(), 220.0f, CMenus::FriendsCategoryEditPopupHeight() + CUi::PopupMenuContentInset(), *Ui()->Screen());
				Ui()->DoPopupMenu(&m_FriendsCategoryPopupContext, Panel.x, Panel.y, Panel.w, Panel.h, &m_FriendsCategoryPopupContext, PopupFriendsCategory, FriendsPopupProperties());
			}
			GameClient()->m_Tooltips.DoToolTip(&m_FriendsAddCategoryCreateButton, &CreateCategoryButton, Localize("Create category"));
		}

		ServerFriends.HSplitTop(3.0f, nullptr, &ServerFriends);
		ServerFriends.HSplitTop(18.0f, &Button, &ServerFriends);
		static CButtonContainer s_AddButton;
		char aAddButtonLabel[128];
		if(s_NameInput.IsEmpty() && !s_ClanInput.IsEmpty())
			str_copy(aAddButtonLabel, Localize("Add clan"));
		else
			str_format(aAddButtonLabel, sizeof(aAddButtonLabel), Localize("Add to %s"), LocalizeFriendsCategory(GameClient()->Friends()->GetCategory(m_FriendAddCategoryIndex)));
		if(DoButton_Menu(&s_AddButton, aAddButtonLabel, 0, &Button))
		{
			const char *pCategory = GameClient()->Friends()->GetCategory(m_FriendAddCategoryIndex);
			GameClient()->Friends()->AddFriend(s_NameInput.GetString(), s_ClanInput.GetString(), pCategory);
			s_NameInput.Clear();
			s_ClanInput.Clear();
			FriendlistOnUpdate();
			Client()->ServerBrowserUpdate();
		}
	}
}

CUi::EPopupMenuFunctionResult CMenus::PopupFriendsCategory(void *pContext, CUIRect View, bool Active)
{
	CFriendsCategoryPopupContext *pPopupContext = static_cast<CFriendsCategoryPopupContext *>(pContext);
	CMenus *pMenus = pPopupContext->m_pMenus;
	if(pMenus == nullptr)
		return CUi::POPUP_CLOSE_CURRENT;

	IFriends *pFriends = pMenus->GameClient()->Friends();
	if(pPopupContext->m_CategoryIndex < 0 || pPopupContext->m_CategoryIndex >= pFriends->NumCategories())
		return CUi::POPUP_CLOSE_CURRENT;

	const char *pCategory = pFriends->GetCategory(pPopupContext->m_CategoryIndex);
	const bool IsProtectedCategory = IsProtectedFriendsCategory(pCategory);
	const float FontSize = ui_token::font::SMALL + 1.0f;

	View.Margin(5.0f, &View);

	if(pPopupContext->m_Mode == CFriendsCategoryPopupContext::MODE_ACTIONS)
	{
		const SQmDropdownVisualStyle Style = FriendsPopupVisualStyle();
		const auto ButtonColor = [&](const CButtonContainer *pButton, bool Enabled = true) {
			const bool Highlighted = Enabled && (pMenus->Ui()->HotItem() == pButton || pMenus->Ui()->CheckActiveItem(pButton));
			return Style.m_ActiveEntryColor.WithAlpha(Highlighted ? Style.m_ActiveEntryColor.a : 0.0f);
		};
		CUIRect Label, Button;
		View.HSplitTop(12.0f, &Label, &View);
		SLabelProperties LabelProps;
		LabelProps.m_MaxWidth = Label.w;
		LabelProps.m_EllipsisAtEnd = true;
		pMenus->Ui()->DoLabel(&Label, LocalizeFriendsCategory(pCategory), FontSize + 1.0f, TEXTALIGN_ML, LabelProps);

		View.HSplitTop(3.0f, nullptr, &View);
		View.HSplitTop(18.0f, &Button, &View);
		if(pMenus->Ui()->DoButton_PopupMenu(&pPopupContext->m_AddButton, Localize("Add category"), &Button, FontSize, TEXTALIGN_MC, 0.0f, false, true, ButtonColor(&pPopupContext->m_AddButton), ui_token::font::SMALL))
		{
			pPopupContext->m_Mode = CFriendsCategoryPopupContext::MODE_ADD;
			pPopupContext->m_NameInput.Clear();
			return CUi::POPUP_KEEP_OPEN;
		}

		View.HSplitTop(3.0f, nullptr, &View);
		View.HSplitTop(18.0f, &Button, &View);
		const ColorRGBA PreviousTextColor = pMenus->TextRender()->GetTextColor();
		if(IsProtectedCategory)
			pMenus->TextRender()->TextColor(ui_token::color::TEXT_DISABLED);
		if(pMenus->Ui()->DoButton_PopupMenu(&pPopupContext->m_RenameButton, Localize("Rename"), &Button, FontSize, TEXTALIGN_MC, 0.0f, false, !IsProtectedCategory, ButtonColor(&pPopupContext->m_RenameButton, !IsProtectedCategory), ui_token::font::SMALL))
		{
			pPopupContext->m_Mode = CFriendsCategoryPopupContext::MODE_RENAME;
			pPopupContext->m_NameInput.Set(pCategory);
			pPopupContext->m_NameInput.SelectAll();
			return CUi::POPUP_KEEP_OPEN;
		}

		View.HSplitTop(3.0f, nullptr, &View);
		View.HSplitTop(18.0f, &Button, &View);
		if(pMenus->Ui()->DoButton_PopupMenu(&pPopupContext->m_DeleteButton, Localize("Delete category"), &Button, FontSize, TEXTALIGN_MC, 0.0f, false, !IsProtectedCategory, ButtonColor(&pPopupContext->m_DeleteButton, !IsProtectedCategory), ui_token::font::SMALL))
		{
			if(pFriends->RemoveCategory(pCategory))
				pMenus->FriendlistOnUpdate();
			return CUi::POPUP_CLOSE_CURRENT;
		}

		pMenus->TextRender()->TextColor(PreviousTextColor);
		return CUi::POPUP_KEEP_OPEN;
	}

	CUIRect Label, Input, Buttons, Cancel, Confirm;
	View.HSplitTop(12.0f, &Label, &View);
	pMenus->Ui()->DoLabel(&Label, pPopupContext->m_Mode == CFriendsCategoryPopupContext::MODE_ADD ? Localize("Category name") : Localize("New category name"), FontSize, TEXTALIGN_ML);

	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(18.0f, &Input, &View);
	const IUiContext FriendsCategoryTextInputCtx = pMenus->SettingsUiContext("server_browser_friends_category_text_input");
	ui_widget::SInputFieldOptions CategoryInputOptions;
	CategoryInputOptions.m_FontSize = FontSize + 1.0f;
	ui_widget::InputField(FriendsCategoryTextInputCtx, &pPopupContext->m_NameInput, Input, CategoryInputOptions);

	View.HSplitTop(6.0f, nullptr, &View);
	View.HSplitTop(20.0f, &Buttons, &View);
	Buttons.VSplitMid(&Cancel, &Confirm, 5.0f);

	const bool CancelPressed = ui_widget::SecondaryButton(FriendsCategoryTextInputCtx, &pPopupContext->m_CancelButton, Localize("Cancel"), Cancel) || (Active && pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE));
	if(CancelPressed)
		return CUi::POPUP_CLOSE_CURRENT;

	const bool ConfirmPressed = ui_widget::PrimaryButton(FriendsCategoryTextInputCtx, &pPopupContext->m_ConfirmButton, pPopupContext->m_Mode == CFriendsCategoryPopupContext::MODE_ADD ? Localize("Add") : Localize("Rename"), Confirm) || (Active && pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER));
	if(ConfirmPressed)
	{
		char aCategory[IFriends::MAX_FRIEND_CATEGORY_LENGTH];
		str_copy(aCategory, str_utf8_skip_whitespaces(pPopupContext->m_NameInput.GetString()), sizeof(aCategory));
		str_utf8_trim_right(aCategory);

		if(aCategory[0] != '\0')
		{
			bool Changed = false;
			if(pPopupContext->m_Mode == CFriendsCategoryPopupContext::MODE_ADD)
				Changed = pFriends->AddCategory(aCategory);
			else
				Changed = pFriends->RenameCategory(pCategory, aCategory);

			if(Changed)
			{
				pMenus->FriendlistOnUpdate();
				const int NewCategory = pFriends->FindCategory(aCategory);
				if(NewCategory >= 0)
					pMenus->m_FriendAddCategoryIndex = NewCategory;
			}
		}

		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CMenus::PopupFriendNote(void *pContext, CUIRect View, bool Active)
{
	CFriendNotePopupContext *pPopupContext = static_cast<CFriendNotePopupContext *>(pContext);
	CMenus *pMenus = pPopupContext->m_pMenus;
	if(pMenus == nullptr)
		return CUi::POPUP_CLOSE_CURRENT;

	const float FontSize = ui_token::font::SMALL + 1.0f;
	View.Margin(5.0f, &View);

	CUIRect Label, Input, Buttons, Cancel, Confirm;
	View.HSplitTop(12.0f, &Label, &View);
	pMenus->Ui()->DoLabel(&Label, Localize("Friend note"), FontSize, TEXTALIGN_ML);

	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(18.0f, &Input, &View);
	const IUiContext FriendNoteTextInputCtx = pMenus->SettingsUiContext("server_browser_friend_note_text_input");
	ui_widget::SInputFieldOptions NoteInputOptions;
	NoteInputOptions.m_FontSize = FontSize + 1.0f;
	ui_widget::InputField(FriendNoteTextInputCtx, &pPopupContext->m_NoteInput, Input, NoteInputOptions);

	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(18.0f, &Buttons, &View);
	Buttons.VSplitMid(&Cancel, &Confirm, 3.0f);

	const bool CancelPressed = ui_widget::SecondaryButton(FriendNoteTextInputCtx, &pPopupContext->m_CancelButton, Localize("Cancel"), Cancel) || (Active && pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE));
	if(CancelPressed)
		return CUi::POPUP_CLOSE_CURRENT;

	const bool ConfirmPressed = ui_widget::PrimaryButton(FriendNoteTextInputCtx, &pPopupContext->m_ConfirmButton, Localize("Save"), Confirm) || (Active && pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER));
	if(ConfirmPressed)
	{
		char aNote[IFriends::MAX_FRIEND_NOTE_LENGTH];
		str_copy(aNote, pPopupContext->m_NoteInput.GetString(), sizeof(aNote));
		str_utf8_trim_right(aNote);

		if(aNote[0] == '\0')
			pMenus->GameClient()->Friends()->ClearFriendNote(pPopupContext->m_aName, pPopupContext->m_aClan);
		else
			pMenus->GameClient()->Friends()->SetFriendNote(pPopupContext->m_aName, pPopupContext->m_aClan, aNote);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

void CMenus::RefreshFriendsCategoryNames()
{
	const int NumCategories = maximum(1, GameClient()->Friends()->NumCategories());
	m_vFriendsCategoryNames.clear();
	m_vFriendsCategoryNames.reserve(NumCategories);
	for(int CategoryIndex = 0; CategoryIndex < NumCategories; ++CategoryIndex)
		m_vFriendsCategoryNames.emplace_back(GameClient()->Friends()->GetCategory(CategoryIndex));
}

void CMenus::ApplyFriendsCategoryExpandedState()
{
	const int NumCategories = maximum(1, GameClient()->Friends()->NumCategories());
	m_vFriendsCategoryExpanded.assign(NumCategories, true);

	const char *pState = g_Config.m_ClFriendsCategoryExpanded;
	const int StateLen = str_length(pState);
	for(int CategoryIndex = 0; CategoryIndex < NumCategories && CategoryIndex < StateLen; ++CategoryIndex)
	{
		if(pState[CategoryIndex] == '0')
			m_vFriendsCategoryExpanded[CategoryIndex] = false;
		else if(pState[CategoryIndex] == '1')
			m_vFriendsCategoryExpanded[CategoryIndex] = true;
	}

	RefreshFriendsCategoryNames();
	m_FriendsCategoryExpandedStateCache = g_Config.m_ClFriendsCategoryExpanded;
	m_FriendsCategoryExpandedLoaded = true;
}

void CMenus::SaveFriendsCategoryExpandedState()
{
	const int NumCategories = maximum(1, GameClient()->Friends()->NumCategories());
	char aState[sizeof(g_Config.m_ClFriendsCategoryExpanded)];
	int Pos = 0;
	for(int CategoryIndex = 0; CategoryIndex < NumCategories && Pos + 1 < (int)sizeof(aState); ++CategoryIndex)
	{
		const bool Expanded = CategoryIndex < (int)m_vFriendsCategoryExpanded.size() && m_vFriendsCategoryExpanded[CategoryIndex] != 0;
		aState[Pos++] = Expanded ? '1' : '0';
	}
	aState[Pos] = '\0';

	while(Pos > 0 && aState[Pos - 1] == '1')
	{
		--Pos;
		aState[Pos] = '\0';
	}

	if(str_comp(aState, g_Config.m_ClFriendsCategoryExpanded) != 0)
		str_copy(g_Config.m_ClFriendsCategoryExpanded, aState, sizeof(g_Config.m_ClFriendsCategoryExpanded));

	m_FriendsCategoryExpandedStateCache = g_Config.m_ClFriendsCategoryExpanded;
	m_FriendsCategoryExpandedLoaded = true;
}

void CMenus::FriendlistOnUpdate()
{
	const int NumCategories = maximum(1, GameClient()->Friends()->NumCategories());
	const bool ConfigChanged = m_FriendsCategoryExpandedLoaded &&
				   str_comp(m_FriendsCategoryExpandedStateCache.c_str(), g_Config.m_ClFriendsCategoryExpanded) != 0;

	if(!m_FriendsCategoryExpandedLoaded || ConfigChanged)
	{
		ApplyFriendsCategoryExpandedState();
	}
	else
	{
		if((int)m_vFriendsCategoryNames.size() != NumCategories)
		{
			std::vector<unsigned char> vExpanded(NumCategories, true);
			for(int CategoryIndex = 0; CategoryIndex < NumCategories; ++CategoryIndex)
			{
				const char *pCategory = GameClient()->Friends()->GetCategory(CategoryIndex);
				for(size_t PrevIndex = 0; PrevIndex < m_vFriendsCategoryNames.size(); ++PrevIndex)
				{
					if(str_comp_nocase(m_vFriendsCategoryNames[PrevIndex].c_str(), pCategory) == 0)
					{
						if(PrevIndex < m_vFriendsCategoryExpanded.size())
							vExpanded[CategoryIndex] = m_vFriendsCategoryExpanded[PrevIndex];
						break;
					}
				}
			}
			m_vFriendsCategoryExpanded.swap(vExpanded);
			RefreshFriendsCategoryNames();
			SaveFriendsCategoryExpandedState();
		}
		else
		{
			bool NamesChanged = false;
			for(int CategoryIndex = 0; CategoryIndex < NumCategories; ++CategoryIndex)
			{
				const char *pCategory = GameClient()->Friends()->GetCategory(CategoryIndex);
				if(str_comp(m_vFriendsCategoryNames[CategoryIndex].c_str(), pCategory) != 0)
				{
					NamesChanged = true;
					break;
				}
			}

			if(NamesChanged)
			{
				RefreshFriendsCategoryNames();
				SaveFriendsCategoryExpandedState();
			}
			else if((int)m_vFriendsCategoryExpanded.size() < NumCategories)
			{
				m_vFriendsCategoryExpanded.resize(NumCategories, true);
			}
			else if((int)m_vFriendsCategoryExpanded.size() > NumCategories)
			{
				m_vFriendsCategoryExpanded.resize(NumCategories);
			}
		}
	}

	if(m_FriendAddCategoryIndex < 0 || m_FriendAddCategoryIndex >= NumCategories)
		m_FriendAddCategoryIndex = 0;

	if(IsClanMembersCategory(GameClient()->Friends()->GetCategory(m_FriendAddCategoryIndex)))
	{
		const int DefaultCategoryIndex = GameClient()->Friends()->FindCategory(GameClient()->Friends()->DefaultCategory());
		m_FriendAddCategoryIndex = DefaultCategoryIndex >= 0 ? DefaultCategoryIndex : 0;
	}
}

void CMenus::PopupConfirmRemoveFriend()
{
	if(!m_HasRemoveFriend)
		return;

	GameClient()->Friends()->RemoveFriend(m_RemoveFriendState == IFriends::FRIEND_PLAYER ? m_aRemoveFriendName : "", m_aRemoveFriendClan);
	FriendlistOnUpdate();
	Client()->ServerBrowserUpdate();
	PopupCancelRemoveFriend();
}

void CMenus::PopupCancelRemoveFriend()
{
	m_HasRemoveFriend = false;
	m_aRemoveFriendName[0] = '\0';
	m_aRemoveFriendClan[0] = '\0';
	m_RemoveFriendState = IFriends::FRIEND_NO;
}

namespace
{
	// 收藏地图页与统计数据页共用同一条工作区页签；选中项放在文件级，
	// 从统计数据切回收藏地图时仍停在上次看的工作区。
	int s_FavoriteMapsWorkspaceTab = 0;
	CButtonContainer s_aFavoriteMapsWorkspaceTabButtons[4];
}

int CMenus::DoMapWorkspaceTabs(const CUIRect &Segments, float Rounding, int CurrentTab, float FontSize)
{
	// 本地存档列表的计数与收藏地图页共用同一份缓存，这里沿用同样的节流刷新。
	char aSavesPath[IO_MAX_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, SAVES_FILE, aSavesPath, sizeof(aSavesPath));
	if(auto pJob = m_LocalSaveDisplay.Refresh(aSavesPath, time_get(), time_freq() * 2))
		Engine()->AddJob(std::move(pJob));

	char aaTabLabels[4][128];
	str_format(aaTabLabels[0], sizeof(aaTabLabels[0]), "%s (%d)", Localize("Favorite map"), (int)GameClient()->m_TClient.GetFavoriteMaps().size());
	str_format(aaTabLabels[1], sizeof(aaTabLabels[1]), "%s (%d)", Localize("Map play history"), (int)GameClient()->m_TClient.GetMapHistory().Entries().size());
	str_format(aaTabLabels[2], sizeof(aaTabLabels[2]), "%s (%d)", Localize("Local saves"), (int)m_LocalSaveDisplay.Entries().size());
	str_format(aaTabLabels[3], sizeof(aaTabLabels[3]), "%s", Localize("Statistics"));
	const char *apTabLabels[4] = {aaTabLabels[0], aaTabLabels[1], aaTabLabels[2], aaTabLabels[3]};
	// 胶囊滑块配色按浏览器面板表面自适应，两个页面都用同一档面板色。
	const ui_widget::SCapsuleTabBarStyle TabStyle = CapsuleTabBarStyleFor(BrowserPanelColor(0.68f));
	const int NewTab = DoNavigationTabs(s_aFavoriteMapsWorkspaceTabButtons, apTabLabels, 4, CurrentTab, Segments, Rounding, &TabStyle, FontSize);
	if(NewTab == 3)
		SetMenuPage(PAGE_STATS);
	else if(NewTab != CurrentTab)
		s_FavoriteMapsWorkspaceTab = NewTab;
	return NewTab;
}

int CMenus::MapWorkspaceTab()
{
	return s_FavoriteMapsWorkspaceTab;
}

void CMenus::RenderServerbrowserFavoriteMaps(CUIRect View)
{
	// 工作区不再自算外缩：View 已对齐全局安全区，与服务器列表面板同一条基线，
	// 页签条与下方正文面板因此贴着同一条左缘。
	const QmMapHistoryUi::SWorkspaceMetrics Layout = QmMapHistoryUi::WorkspaceMetrics(View.h);

	char aSavesPath[IO_MAX_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, SAVES_FILE, aSavesPath, sizeof(aSavesPath));
	if(auto pJob = m_LocalSaveDisplay.Refresh(aSavesPath, time_get(), time_freq() * 2))
		Engine()->AddJob(std::move(pJob));
	const auto &vSaveEntries = m_LocalSaveDisplay.Entries();
	const bool SaveFileExists = m_LocalSaveDisplay.FileExists();
	const std::set<std::string> &FavoriteMaps = GameClient()->m_TClient.GetFavoriteMaps();
	const std::vector<QmMapHistory::SMapHistoryRecord> &HistoryEntries = GameClient()->m_TClient.GetMapHistory().Entries();
	const ColorRGBA TabInactiveColor = BrowserPanelColor(0.68f);

	// 页签字体与设置页胶囊页签同档：DoNavigationTabs 不传字号时按行高反推，
	// 反推值比设置页大一档，两页并排看得出差别。
	CUIRect WorkspaceTabs;
	View.HSplitTop(Layout.m_TabHeight, &WorkspaceTabs, &View);
	DoMapWorkspaceTabs(WorkspaceTabs, Layout.m_TabHeight * 0.20f, s_FavoriteMapsWorkspaceTab, ui_token::settings::TAB_FONT_SIZE);
	View.HSplitTop(Layout.m_SectionGap, nullptr, &View);

	View.Draw(BrowserPanelColor(0.82f), IGraphics::CORNER_ALL, Layout.m_PanelMargin);
	CUiScopedSurfaceText SurfaceText(TextRender(), BrowserPanelColor(0.82f));
	View.Margin(Layout.m_PanelMargin, &View);
	const char *apWorkspaceTitles[] = {
		Localize("Favorite map"),
		Localize("Map play history"),
		Localize("Local saves"),
	};
	const char *apWorkspaceSubTitles[] = {
		Localize("Your favorite maps appear here"),
		Localize("Unfinished maps are shown first"),
		aSavesPath,
	};
	CUIRect WorkspaceHeader, WorkspaceTitle, WorkspaceSubTitle;
	View.HSplitTop(Layout.m_HeaderHeight, &WorkspaceHeader, &View);
	WorkspaceHeader.HSplitTop(Layout.m_HeaderHeight * 0.62f, &WorkspaceTitle, &WorkspaceSubTitle);
	const float HeaderTextInset = Layout.m_PanelMargin * 0.50f;
	WorkspaceTitle.VMargin(HeaderTextInset, &WorkspaceTitle);
	WorkspaceSubTitle.VMargin(HeaderTextInset, &WorkspaceSubTitle);
	// 工作区标题与设置页顶部标题同一档字号，并带前置图标（三个工作区都与地图有关）。
	{
		const char *pWorkspaceIcon = FontIcons::FONT_ICON_MAP;
		const float TitleFontSize = ui_token::font::TITLE;
		const float TitleIconSize = TitleFontSize;
		const float TitleGap = ui_token::page_title::ICON_GAP * 0.5f;
		CUIRect TitleIconRect{WorkspaceTitle.x, WorkspaceTitle.y + (WorkspaceTitle.h - TitleIconSize) * 0.5f, TitleIconSize, TitleIconSize};
		Ui()->DoLabel_QmIcon(&TitleIconRect, EQmIcon::MAP, pWorkspaceIcon, TitleIconSize, TEXTALIGN_ML);
		CUIRect TitleTextRect = WorkspaceTitle;
		TitleTextRect.VSplitLeft(TitleIconSize + TitleGap, nullptr, &TitleTextRect);
		SLabelProperties TitleProps;
		TitleProps.m_MaxWidth = TitleTextRect.w;
		TitleProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&TitleTextRect, apWorkspaceTitles[s_FavoriteMapsWorkspaceTab], TitleFontSize, TEXTALIGN_ML, TitleProps);
	}
	Ui()->DoLabel(&WorkspaceSubTitle, apWorkspaceSubTitles[s_FavoriteMapsWorkspaceTab], std::clamp(WorkspaceSubTitle.h * 0.78f, 8.0f, 10.0f), TEXTALIGN_ML);
	View.HSplitTop(Layout.m_SectionGap, nullptr, &View);

	CUIRect FavoritePanel, HistoryPanel, SavesPanel;
	if(s_FavoriteMapsWorkspaceTab == 0)
		FavoritePanel = View;
	else if(s_FavoriteMapsWorkspaceTab == 1)
		HistoryPanel = View;
	else
		SavesPanel = View;

	static std::unordered_map<std::string, std::string> s_MapCategories;
	static int s_MapCategoryScanIndex = 0;
	static int s_LastNumServers = -1;
	static float s_NextFullScan = 0.0f;
	IServerBrowser *pServerBrowser = ServerBrowser();
	const int NumServers = pServerBrowser ? pServerBrowser->NumSortedServers() : 0;
	if(NumServers != s_LastNumServers)
	{
		s_LastNumServers = NumServers;
		s_MapCategoryScanIndex = 0;
		s_NextFullScan = 0.0f;
	}
	const float LocalNow = Client()->LocalTime();
	if(s_FavoriteMapsWorkspaceTab == 0 && NumServers > 0 && (LocalNow >= s_NextFullScan || s_MapCategoryScanIndex > 0))
	{
		if(s_MapCategoryScanIndex == 0)
		{
			s_MapCategories.clear();
			s_MapCategories.reserve((size_t)NumServers);
		}
		constexpr int ServersPerFrame = 64;
		int ProcessedServers = 0;
		while(s_MapCategoryScanIndex < NumServers && ProcessedServers < ServersPerFrame)
		{
			const CServerInfo *pInfo = pServerBrowser->SortedGet(s_MapCategoryScanIndex);
			++s_MapCategoryScanIndex;
			++ProcessedServers;
			if(!pInfo || pInfo->m_aMap[0] == '\0')
				continue;
			const char *pCategoryKey = FavoriteMapCategoryKeyFromText(pInfo->m_aCommunityType);
			if(!pCategoryKey)
				pCategoryKey = FavoriteMapCategoryKeyFromText(pInfo->m_aName);
			if(!pCategoryKey)
				continue;
			auto It = s_MapCategories.find(pInfo->m_aMap);
			if(It == s_MapCategories.end() || It->second != pCategoryKey)
			{
				s_MapCategories[pInfo->m_aMap] = pCategoryKey;
				GameClient()->m_TClient.UpdateMapCategoryCache(pInfo->m_aMap, pCategoryKey);
			}
		}
		if(s_MapCategoryScanIndex >= NumServers)
		{
			s_MapCategoryScanIndex = 0;
			s_NextFullScan = LocalNow + 2.0f;
		}
	}

	auto GetFavoriteMapCategory = [&](const char *pMapName) -> const char * {
		if(!pMapName || pMapName[0] == '\0')
			return Localize("Unknown");
		const auto It = s_MapCategories.find(pMapName);
		if(It != s_MapCategories.end() && !It->second.empty())
			return FavoriteMapCategoryDisplayName(It->second.c_str());
		const char *pCachedCategory = GameClient()->m_TClient.GetCachedMapCategoryKey(pMapName);
		if(pCachedCategory)
			return FavoriteMapCategoryDisplayName(pCachedCategory);
		return Localize("Unknown");
	};

	auto GetFavoriteMapDifficulty = [&](const char *pMapName, char *pOut, int OutSize) {
		const CQmMapDifficultyCatalog::SEntry *pDifficulty = m_MapDifficultyCatalog.Find(pMapName);
		if(pDifficulty != nullptr)
			str_format(pOut, OutSize, "%d/5 ★", pDifficulty->m_Stars);
		else
			str_copy(pOut, Localize("Unknown"), OutSize);
	};

	auto HasLocalSaveForMap = [&](const char *pMapName) {
		if(!SaveFileExists || !pMapName || pMapName[0] == '\0')
			return false;
		for(const qm_local_saves::SLocalSaveDisplayEntry &Entry : vSaveEntries)
		{
			if(!Entry.m_Map.empty() && str_comp(Entry.m_Map.c_str(), pMapName) == 0)
				return true;
		}
		return false;
	};

	auto DoFavoriteMapColumnLabel = [this](CUIRect Rect, const char *pText, float FontSize, int Align = TEXTALIGN_ML) {
		Rect.VMargin(4.0f, &Rect);
		SLabelProperties Props;
		Props.m_MaxWidth = Rect.w;
		Props.m_StopAtEnd = true;
		Ui()->DoLabel(&Rect, pText, FontSize, Align, Props);
	};

	auto SplitFavoriteMapColumns = [](CUIRect Row, CUIRect *pMap, CUIRect *pCategory, CUIRect *pDifficulty, CUIRect *pNote, CUIRect *pSaved) {
		const float MapWidth = std::clamp(Row.w * 0.24f, 130.0f, 230.0f);
		const float CategoryWidth = std::clamp(Row.w * 0.15f, 86.0f, 130.0f);
		const float DifficultyWidth = std::clamp(Row.w * 0.13f, 78.0f, 120.0f);
		const float SavedWidth = std::clamp(Row.w * 0.10f, 62.0f, 86.0f);
		Row.VSplitLeft(MapWidth, pMap, &Row);
		Row.VSplitLeft(CategoryWidth, pCategory, &Row);
		Row.VSplitLeft(DifficultyWidth, pDifficulty, &Row);
		Row.VSplitRight(SavedWidth, pNote, pSaved);
	};

	if(s_FavoriteMapsWorkspaceTab == 0)
	{
		// 按地图名保留控件身份，删除一行不会把双击状态转移到下一张地图。
		struct SFavoriteMapRow
		{
			int m_RowId = 0;
			int m_NameId = 0;
			CButtonContainer m_RemoveButton;
		};
		static std::map<std::string, SFavoriteMapRow> s_MapRows;
		static std::string s_LastClickedMap;
		std::erase_if(s_MapRows, [&](const auto &Entry) { return !FavoriteMaps.contains(Entry.first); });
		if(!FavoriteMaps.contains(s_LastClickedMap))
			s_LastClickedMap.clear();
		if(FavoriteMaps.empty())
		{
			Ui()->DoLabel(&FavoritePanel, Localize("No favorite maps yet"), 13.0f, TEXTALIGN_MC);
		}
		else
		{
			const int NumFavoriteMaps = (int)FavoriteMaps.size();
			CUIRect HeaderRow;
			FavoritePanel.HSplitTop(20.0f, &HeaderRow, &FavoritePanel);
			HeaderRow.VSplitRight(24.0f, &HeaderRow, nullptr);
			CUIRect HeaderMap, HeaderCategory, HeaderDifficulty, HeaderNote, HeaderSaved;
			SplitFavoriteMapColumns(HeaderRow, &HeaderMap, &HeaderCategory, &HeaderDifficulty, &HeaderNote, &HeaderSaved);
			TextRender()->TextColor(0.75f, 0.75f, 0.75f, 1.0f);
			DoFavoriteMapColumnLabel(HeaderMap, Localize("Map"), 10.0f);
			DoFavoriteMapColumnLabel(HeaderCategory, Localize("Category"), 10.0f);
			DoFavoriteMapColumnLabel(HeaderDifficulty, Localize("Difficulty stars"), 10.0f);
			DoFavoriteMapColumnLabel(HeaderNote, Localize("Note"), 10.0f);
			DoFavoriteMapColumnLabel(HeaderSaved, Localize("Has save"), 10.0f, TEXTALIGN_MR);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			static CListBox s_FavoriteMapsListBox;
			static std::string s_CopiedMap;
			static float s_CopiedAt = 0.0f;
			std::string RemoveMapName;
			s_FavoriteMapsListBox.DoStart(34.0f, NumFavoriteMaps, 1, 3, -1, &FavoritePanel, false, IGraphics::CORNER_NONE);

			for(const std::string &MapName : FavoriteMaps)
			{
				SFavoriteMapRow &RowState = s_MapRows[MapName];
				const CListboxItem Item = s_FavoriteMapsListBox.DoNextItem(&RowState.m_RowId, false);
				if(Item.m_Visible)
				{
					auto GaussianBlurSuppression = Item.SuppressGaussianBlur();
					CUIRect Row = Item.m_Rect;
					Row.Margin(4.0f, &Row);
					Row.Draw(BrowserOpacityColor(ui_token::color::FAVORITE.WithAlpha(0.08f)), IGraphics::CORNER_ALL, ui_token::radius::SMALL_MENU);
					Row.Margin(6.0f, &Row);
					CUIRect RemoveButton;
					Row.VSplitRight(24.0f, &Row, &RemoveButton);
					RemoveButton.VMargin(2.0f, &RemoveButton);
					CUIRect MapColumn, CategoryColumn, DifficultyColumn, NoteColumn, SavedColumn;
					SplitFavoriteMapColumns(Row, &MapColumn, &CategoryColumn, &DifficultyColumn, &NoteColumn, &SavedColumn);

					char aDifficulty[32];
					GetFavoriteMapDifficulty(MapName.c_str(), aDifficulty, sizeof(aDifficulty));
					const char *pNote = GameClient()->m_TClient.GetMapNote(MapName.c_str());
					const char *pSaved = !m_LocalSaveDisplay.Ready() ? Localize("Loading") : (HasLocalSaveForMap(MapName.c_str()) ? Localize("Yes") : Localize("No"));

					if(Ui()->DoButtonLogic(&RowState.m_NameId, 0, &MapColumn, BUTTONFLAG_LEFT))
					{
						Input()->SetClipboardText(MapName.c_str());
						s_CopiedMap = MapName;
						s_CopiedAt = Client()->LocalTime();
						const bool DoubleClicked = Ui()->DoDoubleClickLogic(&RowState.m_NameId) && s_LastClickedMap == MapName;
						s_LastClickedMap = MapName;
						const std::string Command = QmFavoriteMapVoteCommand(MapName, Client()->State() == IClient::STATE_ONLINE, DoubleClicked);
						if(!Command.empty())
						{
							GameClient()->m_Voting.ClearUnfinishedMapVoteChain();
							GameClient()->m_Chat.SendChat(0, Command.c_str());
						}
					}
					const bool Copied = s_CopiedMap == MapName && Client()->LocalTime() - s_CopiedAt < 1.5f;
					GameClient()->m_Tooltips.DoToolTip(&RowState.m_NameId, &MapColumn, Copied ? Localize("Copied") : Localize("Click to copy the map name"));
					if(Ui()->DoButton_QmIcon(&RowState.m_RemoveButton, EQmIcon::CLOSE, FONT_ICON_XMARK, 0, &RemoveButton, BUTTONFLAG_LEFT))
					{
						RemoveMapName = MapName;
						s_LastClickedMap.clear();
					}
					GameClient()->m_Tooltips.DoToolTip(&RowState.m_RemoveButton, &RemoveButton, Localize("Remove from favorites"));

					TextRender()->TextColor(Copied ? ColorRGBA(0.0f, 1.0f, 0.0f, 1.0f) : ColorRGBA(1.0f, 0.85f, 0.0f, 1.0f));
					DoFavoriteMapColumnLabel(MapColumn, MapName.c_str(), 12.0f);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					DoFavoriteMapColumnLabel(CategoryColumn, GetFavoriteMapCategory(MapName.c_str()), 11.0f);
					DoFavoriteMapColumnLabel(DifficultyColumn, aDifficulty, 11.0f);
					DoFavoriteMapColumnLabel(NoteColumn, pNote && pNote[0] != '\0' ? pNote : Localize("None"), 11.0f);
					DoFavoriteMapColumnLabel(SavedColumn, pSaved, 11.0f, TEXTALIGN_MR);
				}
			}
			s_FavoriteMapsListBox.DoEnd();
			// 遍历完成后再删除，避免使收藏集合的迭代器失效。
			if(!RemoveMapName.empty())
				GameClient()->m_TClient.RemoveFavoriteMap(RemoveMapName.c_str());
		}
		return;
	}

	if(s_FavoriteMapsWorkspaceTab == 1)
	{
		static int s_MapHistoryFilter = 0;
		static CButtonContainer s_aMapHistoryFilterButtons[3];
		static CButtonContainer s_ClearFinishedHistoryButton;
		static CButtonContainer s_ClearAllHistoryButton;
		static int s_ClearHistoryConfirmMode = 0;
		static float s_ClearHistoryConfirmUntil = 0.0f;

		if(s_ClearHistoryConfirmMode != 0 && Client()->LocalTime() > s_ClearHistoryConfirmUntil)
			s_ClearHistoryConfirmMode = 0;

		CUIRect FilterArea, ClearArea;
		if(QmMapHistoryUi::StackControls(HistoryPanel.w, Layout.m_ControlHeight))
		{
			CUIRect ControlsArea;
			HistoryPanel.HSplitTop(Layout.m_ControlHeight * 2.0f + Layout.m_SectionGap, &ControlsArea, &HistoryPanel);
			ControlsArea.HSplitTop(Layout.m_ControlHeight, &FilterArea, &ControlsArea);
			ControlsArea.HSplitTop(Layout.m_SectionGap, nullptr, &ControlsArea);
			ControlsArea.HSplitTop(Layout.m_ControlHeight, &ClearArea, nullptr);
		}
		else
		{
			CUIRect ControlsRow;
			HistoryPanel.HSplitTop(Layout.m_ControlHeight, &ControlsRow, &HistoryPanel);
			const float ClearAreaWidth = ControlsRow.w * 0.40f;
			ControlsRow.VSplitRight(ClearAreaWidth, &FilterArea, &ClearArea);
			FilterArea.VSplitRight(Layout.m_SectionGap, &FilterArea, nullptr);
		}

		int UnfinishedCount = 0;
		int FinishedCount = 0;
		for(const QmMapHistory::SMapHistoryRecord &Record : HistoryEntries)
		{
			if(Record.m_Finished)
				++FinishedCount;
			else
				++UnfinishedCount;
		}
		char aaFilterLabels[3][64];
		str_format(aaFilterLabels[0], sizeof(aaFilterLabels[0]), "%s (%d)", Localize("Unfinished"), UnfinishedCount);
		str_format(aaFilterLabels[1], sizeof(aaFilterLabels[1]), "%s (%d)", Localize("Finished"), FinishedCount);
		str_format(aaFilterLabels[2], sizeof(aaFilterLabels[2]), "%s (%d)", Localize("Recent"), (int)HistoryEntries.size());
		// 胶囊滑块配色按浏览器面板表面自适应。
		const char *apFilterLabels[3] = {aaFilterLabels[0], aaFilterLabels[1], aaFilterLabels[2]};
		const ui_widget::SCapsuleTabBarStyle FilterStyle = CapsuleTabBarStyleFor(TabInactiveColor);
		const int NewMapHistoryFilter = DoSegmentedChoice(s_aMapHistoryFilterButtons, apFilterLabels, 3, s_MapHistoryFilter, FilterArea, 5.0f, &FilterStyle);
		if(NewMapHistoryFilter != s_MapHistoryFilter)
			s_MapHistoryFilter = NewMapHistoryFilter;

		CUIRect ClearFinishedButton, ClearAllButton;
		ClearArea.VSplitMid(&ClearFinishedButton, &ClearAllButton, 4.0f);
		ClearFinishedButton.Margin(2.0f, &ClearFinishedButton);
		ClearAllButton.Margin(2.0f, &ClearAllButton);

		const bool ConfirmFinished = s_ClearHistoryConfirmMode == 1;
		const bool ConfirmAll = s_ClearHistoryConfirmMode == 2;
		if(DoButton_Menu(&s_ClearFinishedHistoryButton, ConfirmFinished ? Localize("Confirm clear finished") : Localize("Clear finished"), 0, &ClearFinishedButton))
		{
			if(ConfirmFinished)
			{
				GameClient()->m_TClient.ClearFinishedMapHistory();
				s_ClearHistoryConfirmMode = 0;
			}
			else
			{
				s_ClearHistoryConfirmMode = 1;
				s_ClearHistoryConfirmUntil = Client()->LocalTime() + 3.0f;
			}
		}
		if(DoButton_Menu(&s_ClearAllHistoryButton, ConfirmAll ? Localize("Confirm clear all") : Localize("Clear all history"), 0, &ClearAllButton))
		{
			if(ConfirmAll)
			{
				GameClient()->m_TClient.ClearAllMapHistory();
				s_ClearHistoryConfirmMode = 0;
			}
			else
			{
				s_ClearHistoryConfirmMode = 2;
				s_ClearHistoryConfirmUntil = Client()->LocalTime() + 3.0f;
			}
		}

		HistoryPanel.HSplitTop(Layout.m_SectionGap, nullptr, &HistoryPanel);

		QmMapHistory::EMapHistoryFilter Filter = QmMapHistory::EMapHistoryFilter::UNFINISHED;
		if(s_MapHistoryFilter == 1)
			Filter = QmMapHistory::EMapHistoryFilter::FINISHED;
		else if(s_MapHistoryFilter == 2)
			Filter = QmMapHistory::EMapHistoryFilter::RECENT;
		static std::vector<size_t> s_vHistoryRecordIndices;
		GameClient()->m_TClient.GetMapHistory().SortedIndices(Filter, s_vHistoryRecordIndices);
		if(s_vHistoryRecordIndices.empty())
		{
			Ui()->DoLabel(&HistoryPanel, HistoryEntries.empty() ? Localize("No map play history yet") : Localize("No results"), 13.0f, TEXTALIGN_MC);
		}
		else
		{
			static CListBox s_MapHistoryListBox;
			static std::vector<int> s_vMapHistoryItemIds;
			static std::vector<CButtonContainer> s_vMapHistoryRemoveButtons;
			s_vMapHistoryItemIds.resize(s_vHistoryRecordIndices.size());
			s_vMapHistoryRemoveButtons.resize(s_vHistoryRecordIndices.size());
			const float CardRowHeight = QmMapHistoryUi::CardRowHeight(HistoryPanel.h);
			const int HistoryGridColumns = QmMapHistoryUi::GridColumns(HistoryPanel.w - QmMapHistoryUi::LIST_SCROLLBAR_WIDTH, CardRowHeight);
			s_MapHistoryListBox.SetScrollbarAlwaysReserved(true);
			s_MapHistoryListBox.DoStart(CardRowHeight, (int)s_vHistoryRecordIndices.size(), HistoryGridColumns, 1, -1, &HistoryPanel, false, IGraphics::CORNER_NONE);

			auto DoHistoryCardLabel = [this](CUIRect Rect, const char *pText, float FontSize, int Align) {
				SLabelProperties Props;
				Props.m_MaxWidth = Rect.w;
				Props.m_StopAtEnd = true;
				Props.m_EllipsisAtEnd = true;
				Ui()->DoLabel(&Rect, pText, FontSize, Align, Props);
			};

			std::string RemoveMapId;
			for(size_t HistoryIndex = 0; HistoryIndex < s_vHistoryRecordIndices.size(); ++HistoryIndex)
			{
				const QmMapHistory::SMapHistoryRecord &Record = HistoryEntries[s_vHistoryRecordIndices[HistoryIndex]];
				const CListboxItem Item = s_MapHistoryListBox.DoNextItem(&s_vMapHistoryItemIds[HistoryIndex], false);
				if(!Item.m_Visible)
					continue;
				auto GaussianBlurSuppression = Item.SuppressGaussianBlur();

				char aDeaths[32];
				str_format(aDeaths, sizeof(aDeaths), "%d", Record.m_DeathCount);
				char aLastEntered[64];
				if(Record.m_LastEnteredAt > 0)
					str_timestamp_ex((time_t)Record.m_LastEnteredAt, aLastEntered, sizeof(aLastEntered), FORMAT_SPACE);
				else if(!Record.m_LastPlayedDate.empty())
					str_copy(aLastEntered, Record.m_LastPlayedDate.c_str(), sizeof(aLastEntered));
				else
					str_copy(aLastEntered, Localize("Unknown"), sizeof(aLastEntered));

				char aTime[64];
				const int64_t TimeMs = Record.m_Finished ? Record.m_FinishTimeMs : Record.m_PlayTimeMs;
				str_time(TimeMs / 10, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));

				const ColorRGBA StatusColor = Record.m_Finished ? ColorRGBA(0.36f, 0.80f, 0.55f, 1.0f) : ColorRGBA(1.0f, 0.78f, 0.18f, 1.0f);
				const ColorRGBA CardColor = Record.m_Finished ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.055f) : ColorRGBA(1.0f, 0.78f, 0.18f, 0.09f);
				const float CardGap = QmMapHistoryUi::CardGap(CardRowHeight);
				CUIRect Card = Item.m_Rect;
				Card.Margin(CardGap * 0.5f, &Card);
				Card.Draw(BrowserOpacityColor(CardColor), IGraphics::CORNER_ALL, CardGap * 0.80f);

				CUIRect Accent, CardContent;
				const float AccentWidth = std::clamp(CardRowHeight * 0.04f, 2.0f, 3.0f);
				Card.VSplitLeft(AccentWidth, &Accent, &CardContent);
				Accent.Draw(BrowserOpacityColor(StatusColor, 2.0f), IGraphics::CORNER_L, CardGap * 0.80f);
				CardContent.Margin(QmMapHistoryUi::CardPadding(CardRowHeight), &CardContent);
				CUIRect CardHeader, MetricsRow;
				CardContent.HSplitTop(CardContent.h * 0.43f, &CardHeader, &MetricsRow);
				MetricsRow.HSplitTop(CardGap * 0.20f, nullptr, &MetricsRow);

				CUIRect MapName, Status, RemoveButton;
				CardHeader.VSplitRight(CardHeader.h, &CardHeader, &RemoveButton);
				const float StatusWidth = std::clamp(CardHeader.w * 0.28f, 48.0f, 68.0f);
				CardHeader.VSplitRight(StatusWidth, &MapName, &Status);
				MapName.VSplitRight(CardGap * 0.40f, &MapName, nullptr);
				DoHistoryCardLabel(MapName, Record.m_MapName.c_str(), std::clamp(CardHeader.h * 0.62f, 10.5f, 12.0f), TEXTALIGN_ML);
				TextRender()->TextColor(StatusColor);
				DoHistoryCardLabel(Status, Record.m_Finished ? Localize("Finished") : Localize("Unfinished"), std::clamp(CardHeader.h * 0.48f, 8.0f, 9.5f), TEXTALIGN_MR);
				TextRender()->TextColor(TextRender()->DefaultTextColor());

				CUIRect PrimaryMetrics, TimeMetric, DeathMetric, LastEnteredMetric;
				MetricsRow.VSplitRight(MetricsRow.w * 0.48f, &PrimaryMetrics, &LastEnteredMetric);
				PrimaryMetrics.VSplitRight(PrimaryMetrics.w * 0.38f, &TimeMetric, &DeathMetric);
				TimeMetric.VSplitRight(CardGap * 0.20f, &TimeMetric, nullptr);
				DeathMetric.VMargin(CardGap * 0.10f, &DeathMetric);
				LastEnteredMetric.VSplitLeft(CardGap * 0.20f, nullptr, &LastEnteredMetric);
				auto RenderMetric = [&](CUIRect Metric, const char *pLabel, const char *pValue, int Align) {
					CUIRect Label, Value;
					Metric.HSplitTop(Metric.h * 0.42f, &Label, &Value);
					TextRender()->TextColor(0.68f, 0.68f, 0.68f, 1.0f);
					DoHistoryCardLabel(Label, pLabel, std::clamp(Label.h * 0.72f, 6.5f, 8.0f), Align);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					DoHistoryCardLabel(Value, pValue, std::clamp(Value.h * 0.72f, 8.0f, 9.5f), Align);
				};
				RenderMetric(TimeMetric, Localize("Time"), aTime, TEXTALIGN_ML);
				RenderMetric(DeathMetric, Localize("Deaths"), aDeaths, TEXTALIGN_MC);
				RenderMetric(LastEnteredMetric, Localize("Last entered"), aLastEntered, TEXTALIGN_MR);

				if(Ui()->DoButton_QmIcon(&s_vMapHistoryRemoveButtons[HistoryIndex], EQmIcon::CLOSE, FONT_ICON_XMARK, 0, &RemoveButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL))
					RemoveMapId = Record.m_MapId;
				if(Ui()->HotItem() == &s_vMapHistoryRemoveButtons[HistoryIndex])
					GameClient()->m_Tooltips.DoToolTip(&s_vMapHistoryRemoveButtons[HistoryIndex], &RemoveButton, Localize("Remove from history"));
			}
			s_MapHistoryListBox.DoEnd();
			if(!RemoveMapId.empty())
				GameClient()->m_TClient.RemoveMapHistoryRecord(RemoveMapId.c_str());
		}
		return;
	}

	if(!m_LocalSaveDisplay.Ready())
	{
		Ui()->DoLabel(&SavesPanel, Localize("Loading"), 13.0f, TEXTALIGN_MC);
		return;
	}
	if(!SaveFileExists)
	{
		Ui()->DoLabel(&SavesPanel, Localize("ddnet-saves.txt not found"), 13.0f, TEXTALIGN_MC);
		return;
	}
	if(vSaveEntries.empty())
	{
		Ui()->DoLabel(&SavesPanel, Localize("ddnet-saves.txt is empty"), 13.0f, TEXTALIGN_MC);
		return;
	}

	const int NumSaveEntries = (int)vSaveEntries.size();
	static CListBox s_SavesListBox;
	static std::vector<int> s_vSaveItemIds;
	s_vSaveItemIds.resize(NumSaveEntries);
	s_SavesListBox.DoStart(42.0f, NumSaveEntries, 1, 3, -1, &SavesPanel, false, IGraphics::CORNER_NONE);

	for(size_t SaveIndex = 0; SaveIndex < vSaveEntries.size(); ++SaveIndex)
	{
		const qm_local_saves::SLocalSaveDisplayEntry &Entry = vSaveEntries[SaveIndex];
		const CListboxItem Item = s_SavesListBox.DoNextItem(&s_vSaveItemIds[SaveIndex], false);
		if(!Item.m_Visible)
			continue;
		auto GaussianBlurSuppression = Item.SuppressGaussianBlur();

		CUIRect Row, TopLine, BottomLine, TimeLabel;
		Item.m_Rect.Margin(4.0f, &Row);
		Row.Draw(BrowserOpacityColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f)), IGraphics::CORNER_ALL, 5.0f);
		Row.Margin(6.0f, &Row);
		Row.HSplitTop(17.0f, &TopLine, &BottomLine);
		BottomLine.VSplitRight(105.0f, &BottomLine, &TimeLabel);

		const char *pMap = Entry.m_Map.empty() ? Entry.m_RawLine.c_str() : Entry.m_Map.c_str();
		SLabelProperties Props;
		Props.m_MaxWidth = TopLine.w;
		Props.m_StopAtEnd = true;
		Ui()->DoLabel(&TopLine, pMap, 12.0f, TEXTALIGN_ML, Props);

		char aDetail[512];
		str_format(aDetail, sizeof(aDetail), "[%s] %s",
			Entry.m_Players.empty() ? "Unknown" : Entry.m_Players.c_str(),
			Entry.m_Code.empty() ? "no-code" : Entry.m_Code.c_str());
		Props.m_MaxWidth = BottomLine.w;
		Ui()->DoLabel(&BottomLine, aDetail, 10.0f, TEXTALIGN_ML, Props);
		Ui()->DoLabel(&TimeLabel, Entry.m_Time.empty() ? "-" : Entry.m_Time.c_str(), 9.0f, TEXTALIGN_MR);
	}
	s_SavesListBox.DoEnd();
}

enum
{
	UI_TOOLBOX_PAGE_FILTERS = 0,
	UI_TOOLBOX_PAGE_INFO,
	UI_TOOLBOX_PAGE_FRIENDS,
	NUM_UI_TOOLBOX_PAGES,
};

static void NormalizeServerbrowserToolboxPage()
{
	// 旧版本把栖梦页签保存为 3；加载旧配置时回到好友页，不进入无效分支。
	if(g_Config.m_UiToolboxPage >= NUM_UI_TOOLBOX_PAGES)
		g_Config.m_UiToolboxPage = UI_TOOLBOX_PAGE_FRIENDS;
}

void CMenus::RenderServerbrowserTabBar(CUIRect TabBar)
{
	NormalizeServerbrowserToolboxPage();
	CUIRect FilterTabButton, InfoTabButton, FriendsTabButton;
	TabBar.VSplitLeft(TabBar.w / 3.0f, &FilterTabButton, &TabBar);
	TabBar.VSplitLeft(TabBar.w / 2.0f, &InfoTabButton, &FriendsTabButton);
	FilterTabButton.VSplitRight(3.0f, &FilterTabButton, nullptr);
	InfoTabButton.VSplitLeft(3.0f, nullptr, &InfoTabButton);
	InfoTabButton.VSplitRight(3.0f, &InfoTabButton, nullptr);
	FriendsTabButton.VSplitLeft(3.0f, nullptr, &FriendsTabButton);
	FriendsTabButton.VSplitRight(3.0f, &FriendsTabButton, nullptr);

	if(!Ui()->IsPopupOpen() && Ui()->ConsumeHotkey(CUi::HOTKEY_TAB))
	{
		const int Direction = Input()->ShiftIsPressed() ? -1 : 1;
		g_Config.m_UiToolboxPage = (g_Config.m_UiToolboxPage + NUM_UI_TOOLBOX_PAGES + Direction) % NUM_UI_TOOLBOX_PAGES;
	}

	// 胶囊 Tabbar：槽位先算完，再画容器与滑块，最后画页签图标/文字。
	const CUIRect aToolboxTabSlots[] = {FilterTabButton, InfoTabButton, FriendsTabButton};
	const int aToolboxTabPages[] = {UI_TOOLBOX_PAGE_FILTERS, UI_TOOLBOX_PAGE_INFO, UI_TOOLBOX_PAGE_FRIENDS};
	int ActiveToolboxTab = -1;
	for(size_t Tab = 0; Tab < std::size(aToolboxTabSlots); ++Tab)
	{
		if(g_Config.m_UiToolboxPage == aToolboxTabPages[Tab])
			ActiveToolboxTab = (int)Tab;
	}
	const IUiContext ToolboxTabBarCtx = TabBarUiContext();
	ActiveToolboxTab = ui_widget::CapsuleTabBar(ToolboxTabBarCtx, MakeUiScopeHash("browser_toolbox_tabs_capsule"), aToolboxTabSlots, std::size(aToolboxTabSlots), ActiveToolboxTab, CapsuleTabBarStyleFor(BrowserPanelColor(1.0f)));
	if(ActiveToolboxTab >= 0)
		g_Config.m_UiToolboxPage = aToolboxTabPages[ActiveToolboxTab];

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

	static CButtonContainer s_FilterTabButton;
	DoButton_MenuTab(&s_FilterTabButton, FONT_ICON_LIST_UL, g_Config.m_UiToolboxPage == UI_TOOLBOX_PAGE_FILTERS, &FilterTabButton, IGraphics::CORNER_ALL, &m_aAnimatorsSmallPage[SMALL_TAB_BROWSER_FILTER], nullptr, nullptr, nullptr, 10.0f, nullptr, nullptr, -1.0f, true);
	GameClient()->m_Tooltips.DoToolTip(&s_FilterTabButton, &FilterTabButton, Localize("Server filter"));

	static CButtonContainer s_InfoTabButton;
	DoButton_MenuTab(&s_InfoTabButton, FONT_ICON_INFO, g_Config.m_UiToolboxPage == UI_TOOLBOX_PAGE_INFO, &InfoTabButton, IGraphics::CORNER_ALL, &m_aAnimatorsSmallPage[SMALL_TAB_BROWSER_INFO], nullptr, nullptr, nullptr, 10.0f, nullptr, nullptr, -1.0f, true);
	GameClient()->m_Tooltips.DoToolTip(&s_InfoTabButton, &InfoTabButton, Localize("Server info"));

	static CButtonContainer s_FriendsTabButton;
	DoButton_MenuTab(&s_FriendsTabButton, FONT_ICON_HEART, g_Config.m_UiToolboxPage == UI_TOOLBOX_PAGE_FRIENDS, &FriendsTabButton, IGraphics::CORNER_ALL, &m_aAnimatorsSmallPage[SMALL_TAB_BROWSER_FRIENDS], nullptr, nullptr, nullptr, 10.0f, nullptr, nullptr, -1.0f, true);
	GameClient()->m_Tooltips.DoToolTip(&s_FriendsTabButton, &FriendsTabButton, Localize("Friends"));

	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

void CMenus::RenderServerbrowserToolBox(CUIRect ToolBox)
{
	NormalizeServerbrowserToolboxPage();
	static int s_PrevToolboxPage = UI_TOOLBOX_PAGE_FILTERS;
	static float s_ToolboxDirection = 0.0f;
	if(g_Config.m_UiToolboxPage != s_PrevToolboxPage)
	{
		s_ToolboxDirection = ResolveUiSwitchDirection(GameClient()->UiRuntimeV2()->AnimRuntime(), UiAnimNodeKey("browser_toolbox_tab_switch"), s_ToolboxDirection, g_Config.m_UiToolboxPage > s_PrevToolboxPage ? 1.0f : -1.0f);
		TriggerUiSwitchAnimation(UiAnimNodeKey("browser_toolbox_tab_switch"), ui_token::motion::TAB_SWITCH.m_DurationSec);
		s_PrevToolboxPage = g_Config.m_UiToolboxPage;
	}

	const float TransitionStrength = ReadUiSwitchAnimation(UiAnimNodeKey("browser_toolbox_tab_switch"));
	const bool TransitionActive = TransitionStrength > 0.0f && s_ToolboxDirection != 0.0f;
	const float TransitionAlpha = UiSwitchAnimationAlpha(TransitionStrength);
	const CUIRect ContentClip = ToolBox;
	if(TransitionActive)
	{
		Ui()->ClipEnable(&ContentClip);
		ApplyUiSwitchOffset(ToolBox, TransitionStrength, s_ToolboxDirection, false, 0.08f, 24.0f, 120.0f);
	}

	switch(g_Config.m_UiToolboxPage)
	{
	case UI_TOOLBOX_PAGE_FILTERS:
		RenderServerbrowserFilters(ToolBox);
		break;
	case UI_TOOLBOX_PAGE_INFO:
		RenderServerbrowserInfo(ToolBox);
		break;
	case UI_TOOLBOX_PAGE_FRIENDS:
		RenderServerbrowserFriends(ToolBox);
		break;
	default:
		RenderServerbrowserFilters(ToolBox);
		break;
	}

	if(TransitionActive)
	{
		if(TransitionAlpha > 0.0f)
			DrawUiSwitchTransitionOverlay(ContentClip, BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha)));
		Ui()->ClipDisable();
	}
}

void CMenus::RenderServerbrowser(CUIRect MainView, bool DrawBackground)
{
	// 切换服务器确认的结果：确认后按 Connect() 记下的地址连过去；
	// 取消、或被页面切换收掉时只清状态，不动当前连接。
	if(s_SwitchServerPending)
	{
		if(s_SwitchServerConfirm.m_Result == CUi::SConfirmPopupContext::CONFIRMED)
		{
			s_SwitchServerPending = false;
			Client()->Connect(m_aNextServer);
		}
		else if(s_SwitchServerConfirm.m_Result == CUi::SConfirmPopupContext::CANCELED || !Ui()->IsPopupOpen(&s_SwitchServerConfirm))
		{
			s_SwitchServerPending = false;
		}
	}

	// 面板颜色自行处理透明度，不再整体缩放独立配置的按钮和输入框。
	// 首次打开菜单时先复用现有 community 选择结果。缓存哈希、过滤器重建
	// 和列表刷新放到下一帧，避免 ESC 首帧同步执行后台元数据整理。
	if(m_MenuOpenFrame > 0)
		UpdateCommunityCache(false);

	switch(g_Config.m_UiPage)
	{
	case PAGE_INTERNET:
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_BROWSER_INTERNET);
		break;
	case PAGE_LAN:
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_BROWSER_LAN);
		if(m_ForceRefreshLanPage)
		{
			RefreshBrowserTab(true);
			m_ForceRefreshLanPage = false;
		}
		break;
	case PAGE_FAVORITES:
	case PAGE_FAVORITE_MAPS:
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_BROWSER_FAVORITES);
		break;
	case PAGE_FAVORITE_COMMUNITY_1:
	case PAGE_FAVORITE_COMMUNITY_2:
	case PAGE_FAVORITE_COMMUNITY_3:
	case PAGE_FAVORITE_COMMUNITY_4:
	case PAGE_FAVORITE_COMMUNITY_5:
		GameClient()->m_MenuBackground.ChangePosition(g_Config.m_UiPage - PAGE_FAVORITE_COMMUNITY_1 + CMenuBackground::POS_BROWSER_CUSTOM0);
		break;
	default:
		dbg_assert_failed("ui_page invalid for RenderServerbrowser: %d", g_Config.m_UiPage);
	}

	// clang-format off
	/*
		+---------------------------+ +---communities---+
		|                           | |                 |
		|                           | +------tabs-------+
		|       server list         | |                 |
		|                           | |      tool       |
		|                           | |      box        |
		+---------------------------+ |                 |
		        status box            +-----------------+
	*/
	// clang-format on

	(void)DrawBackground;
	if(g_Config.m_UiPage == PAGE_FAVORITE_MAPS)
	{
		AddMenuHitRegion(MainView);
		// 新 UI：面板直接对齐全局安全区边缘（距窗口 8px 统一基准），
		// 垂直不额外内缩，面板顶边与其他页面的卡片同线。
		RenderServerbrowserFavoriteMaps(MainView);
		return;
	}

	const CUIRect View = MainView;
	AddMenuHitRegion(View);
	// 状态栏提示行已删除，列表/状态栏分割按「无提示行」算（见 RenderServerbrowserStatusBox 上方的说明）。
	const SQmBrowserPanelLayout PanelLayout = QmBrowserPanelLayout(View, false);
	PanelLayout.m_List.Draw(BrowserPanelColor(), IGraphics::CORNER_ALL, ui_token::radius::CARD);
	PanelLayout.m_Status.Draw(BrowserPanelElevatedColor(), IGraphics::CORNER_ALL, ui_token::radius::CARD);
	if(PanelLayout.m_Toolbox.w > 0.0f)
		PanelLayout.m_Toolbox.Draw(BrowserPanelColor(), IGraphics::CORNER_ALL, ui_token::radius::CARD);
	const CUIRect ServerListBase = PanelLayout.m_ListContent;
	const CUIRect StatusBox = PanelLayout.m_StatusViewport;
	const CUIRect ToolBoxBase = PanelLayout.m_ToolboxContent;
	CUIRect TabBar;

	float TransitionOffset = 0.0f;
	const float TransitionStrength = ReadUiSwitchAnimation(UiAnimNodeKey("browser_page_switch"));
	const bool DoClip = TransitionStrength > 0.0f && m_BrowserTabTransitionDirection != 0.0f;
	float TransitionAlpha = UiSwitchAnimationAlpha(TransitionStrength);
	if(DoClip)
	{
		TransitionOffset = TransitionStrength * std::clamp(View.w * 0.08f, 24.0f, 120.0f) * m_BrowserTabTransitionDirection;
	}

	bool WasListboxItemActivated = false;
	if(ServerListBase.w > 0.0f && ServerListBase.h > ms_ListheaderHeight)
	{
		CUiScopedSurfaceText SurfaceText(TextRender(), BrowserPanelColor());
		CUIRect ServerList = ServerListBase;
		if(DoClip)
		{
			Ui()->ClipEnable(&ServerListBase);
			ServerList.x += TransitionOffset;
		}
		RenderServerbrowserServerList(ServerList, WasListboxItemActivated);
		if(DoClip)
		{
			if(TransitionAlpha > 0.0f)
			{
				DrawUiSwitchTransitionOverlay(ServerListBase, BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha)));
			}
			Ui()->ClipDisable();
		}
	}

	{
		CUiScopedSurfaceText SurfaceText(TextRender(), BrowserPanelElevatedColor());
		RenderServerbrowserStatusBox(StatusBox, WasListboxItemActivated);
	}

	if(ToolBoxBase.w > 0.0f && ToolBoxBase.h > 0.0f)
	{
		CUiScopedSurfaceText SurfaceText(TextRender(), BrowserPanelColor());
		CUIRect ToolBox = ToolBoxBase;
		if(DoClip)
		{
			Ui()->ClipEnable(&ToolBoxBase);
			ToolBox.x += TransitionOffset;
		}

		if(g_Config.m_UiPage == PAGE_INTERNET || g_Config.m_UiPage == PAGE_FAVORITES)
		{
			CUIRect CommunityFilter;
			// 社区卡片 = 标题 + 整数行（胶囊行行高 + 半档行距），末行完整可见。
			ToolBox.HSplitTop(19.0f + FilterGridExtent(COMMUNITY_FILTER_VISIBLE_ROWS, COMMUNITY_FILTER_ROW_HEIGHT), &CommunityFilter, &ToolBox);
			ToolBox.HSplitTop(8.0f, nullptr, &ToolBox);
			RenderServerbrowserCommunitiesFilter(CommunityFilter);
		}

		ToolBox.HSplitTop(24.0f, &TabBar, &ToolBox);
		RenderServerbrowserTabBar(TabBar);
		RenderServerbrowserToolBox(ToolBox);

		if(DoClip)
		{
			if(TransitionAlpha > 0.0f)
			{
				DrawUiSwitchTransitionOverlay(ToolBoxBase, BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha)));
			}
			Ui()->ClipDisable();
		}
	}
}

template<typename F>
bool CMenus::PrintHighlighted(const char *pName, F &&PrintFn)
{
	const char *pStr = g_Config.m_BrFilterString;
	char aFilterStr[sizeof(g_Config.m_BrFilterString)];
	char aFilterStrTrimmed[sizeof(g_Config.m_BrFilterString)];
	while((pStr = str_next_token(pStr, IServerBrowser::SEARCH_EXCLUDE_TOKEN, aFilterStr, sizeof(aFilterStr))))
	{
		str_copy(aFilterStrTrimmed, str_utf8_skip_whitespaces(aFilterStr));
		str_utf8_trim_right(aFilterStrTrimmed);
		// highlight the parts that matches
		const char *pFilteredStr;
		int FilterLen = str_length(aFilterStrTrimmed);
		if(aFilterStrTrimmed[0] == '"' && aFilterStrTrimmed[FilterLen - 1] == '"')
		{
			aFilterStrTrimmed[FilterLen - 1] = '\0';
			pFilteredStr = str_comp(pName, &aFilterStrTrimmed[1]) == 0 ? pName : nullptr;
			FilterLen -= 2;
		}
		else
		{
			const char *pFilteredStrEnd;
			pFilteredStr = str_utf8_find_nocase(pName, aFilterStrTrimmed, &pFilteredStrEnd);
			if(pFilteredStr != nullptr && pFilteredStrEnd != nullptr)
				FilterLen = pFilteredStrEnd - pFilteredStr;
		}
		if(pFilteredStr)
		{
			PrintFn(pFilteredStr, FilterLen);
			return true;
		}
	}
	return false;
}

CTeeRenderInfo CMenus::GetTeeRenderInfo(vec2 Size, const char *pSkinName, bool CustomSkinColors, int CustomSkinColorBody, int CustomSkinColorFeet) const
{
	CTeeRenderInfo TeeInfo;
	TeeInfo.Apply(GameClient()->m_Skins.Find(pSkinName));
	TeeInfo.ApplyColors(CustomSkinColors, CustomSkinColorBody, CustomSkinColorFeet);
	TeeInfo.m_Size = minimum(Size.x, Size.y);
	return TeeInfo;
}

void CMenus::ConchainFriendlistUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CMenus *pThis = ((CMenus *)pUserData);
	if(pResult->NumArguments() >= 1 && (pThis->Client()->State() == IClient::STATE_OFFLINE || pThis->Client()->State() == IClient::STATE_ONLINE))
	{
		pThis->FriendlistOnUpdate();
		pThis->Client()->ServerBrowserUpdate();
	}
}

void CMenus::ConchainFavoritesUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() >= 1 && g_Config.m_UiPage == PAGE_FAVORITES)
		((CMenus *)pUserData)->ServerBrowser()->Refresh(IServerBrowser::TYPE_FAVORITES);
}

void CMenus::ConchainCommunitiesUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CMenus *pThis = static_cast<CMenus *>(pUserData);
	if(pResult->NumArguments() >= 1 && (g_Config.m_UiPage == PAGE_INTERNET || g_Config.m_UiPage == PAGE_FAVORITES || (g_Config.m_UiPage >= PAGE_FAVORITE_COMMUNITY_1 && g_Config.m_UiPage <= PAGE_FAVORITE_COMMUNITY_5)))
	{
		pThis->UpdateCommunityCache(true);
		pThis->Client()->ServerBrowserUpdate();
	}
}

void CMenus::ConchainUiPageUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CMenus *pThis = static_cast<CMenus *>(pUserData);
	if(pResult->NumArguments() >= 1)
	{
		if(g_Config.m_UiPage >= PAGE_FAVORITE_COMMUNITY_1 && g_Config.m_UiPage <= PAGE_FAVORITE_COMMUNITY_5 &&
			(size_t)(g_Config.m_UiPage - PAGE_FAVORITE_COMMUNITY_1) >= pThis->ServerBrowser()->FavoriteCommunities().size())
		{
			// Reset page to internet when there is no favorite community for this page.
			g_Config.m_UiPage = PAGE_INTERNET;
		}

		pThis->SetMenuPage(g_Config.m_UiPage);
	}
}

void CMenus::UpdateCommunityCache(bool Force)
{
	if(g_Config.m_UiPage >= PAGE_FAVORITE_COMMUNITY_1 && g_Config.m_UiPage <= PAGE_FAVORITE_COMMUNITY_5 &&
		(size_t)(g_Config.m_UiPage - PAGE_FAVORITE_COMMUNITY_1) >= ServerBrowser()->FavoriteCommunities().size())
	{
		// Reset page to internet when there is no favorite community for this page,
		// i.e. when favorite community is removed via console while the page is open.
		// This also updates the community cache because the page is changed.
		SetMenuPage(PAGE_INTERNET);
	}
	else
	{
		ServerBrowser()->CommunityCache().Update(Force);
		// 社区数据刷新后，把国家筛选名单与新出现的可选国家对齐，避免"只看某几个国家"的结果漂移。
		auto *pServerBrowser = dynamic_cast<CServerBrowser *>(ServerBrowser());
		if(pServerBrowser != nullptr && pServerBrowser->CountriesFilter().AutoExcludeNewCountries())
		{
			Client()->ServerBrowserUpdate();
		}
	}
}
