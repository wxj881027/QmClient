// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMPLAYERCARD_H
#define GAME_CLIENT_QMUI_QMPLAYERCARD_H

#include "UiButtons.h"
#include "UiOverlays.h"
#include "UiTokens.h"

#include <engine/input.h>
#include <engine/textrender.h>

#include <game/client/ui.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>

// 玩家页右侧的「玩家信息卡」：分区几何 + 两个操作网格。
// 全部按钮都走既有组件（ui_widget::SecondaryButton / ui_widget::DoIconButton），
// 这里只负责「排在哪、多大、点了算哪个动作」，不自己画底色、描边或图标。
// 尺寸一律取 ui_token::player_card，调用点不再写 36 / 22 / 8 这类字面量。
namespace QmPlayersUi
{

	// 卡片操作区（胶囊按钮）的动作：顺序即按钮容器数组的下标。
	enum class EPlayerCardAction
	{
		NONE = -1,
		FRIEND = 0,
		FOLLOW,
		MUTE,
		MUTE_EMOTICONS,
		COPY_SKIN,
		COUNT,
	};

	// 图标复制区的动作：点一下就把对应文本写进剪贴板。
	enum class EPlayerCardCopy
	{
		NONE = -1,
		NAME = 0,
		CLAN,
		SKIN,
		COUNT,
	};

	// 分区尺寸的最高档位来自 token：枚举个数与 token 个数必须一致，
	// 加减一个按钮时两边同时改，不会出现「枚举 5 个、网格按 6 个排」的错位。
	static_assert(static_cast<int>(EPlayerCardAction::COUNT) == ui_token::player_card::ACTION_COUNT, "操作区动作个数必须与 ui_token::player_card::ACTION_COUNT 一致");
	static_assert(static_cast<int>(EPlayerCardCopy::COUNT) == ui_token::player_card::ICON_COUNT, "图标复制区个数必须与 ui_token::player_card::ICON_COUNT 一致");

	struct SPlayerCardLayout
	{
		CUIRect m_Header; // 皮肤、名字、国家
		CUIRect m_Actions; // 胶囊操作区
		CUIRect m_IconRow; // 小方形图标按钮行
		CUIRect m_Status; // 在线 / 旁观 / 加入
		CUIRect m_Details; // 玩家信息细节（占剩余高度）
	};

	// 操作区行数：两列网格向上取整，最后一行只有一个动作时占满整行。
	inline int PlayerCardActionRows(const int ActionCount)
	{
		const int Columns = std::max(1, ui_token::player_card::ACTION_COLUMNS);
		return std::max(0, (ActionCount + Columns - 1) / Columns);
	}

	inline float PlayerCardActionsHeight(const int ActionCount)
	{
		const int Rows = PlayerCardActionRows(ActionCount);
		if(Rows <= 0)
			return 0.0f;
		return Rows * ui_token::player_card::ACTION_HEIGHT + (Rows - 1) * ui_token::player_card::ACTION_GAP;
	}

	// 卡片内容的自然高度：滚动区按它排版，卡片再矮也只是滚动，不会把分区压成负高度。
	inline float PlayerCardContentHeight(const int ActionCount, const int DetailRows)
	{
		return ui_token::player_card::HEADER_HEIGHT +
		       ui_token::player_card::SECTION_GAP +
		       PlayerCardActionsHeight(ActionCount) +
		       ui_token::player_card::SECTION_GAP +
		       ui_token::player_card::ICON_BUTTON_SIZE +
		       ui_token::player_card::SECTION_GAP +
		       ui_token::player_card::STATUS_HEIGHT +
		       ui_token::player_card::SECTION_GAP +
		       std::max(0, DetailRows) * ui_token::player_card::DETAIL_ROW_HEIGHT;
	}

	// 分区自上而下依次切分：每个分区都被夹在当前剩余高度内，
	// 高度不够时后面的分区直接归零，保证任何窗口尺寸下都不会越出卡片。
	inline SPlayerCardLayout PlayerCardLayout(const CUIRect &Card, const int ActionCount)
	{
		SPlayerCardLayout Layout;
		CUIRect Body = Card;
		float Remaining = std::max(0.0f, Body.h);
		const auto Take = [&](const float Height) {
			CUIRect Rect{Body.x, Body.y, Body.w, std::max(0.0f, std::min(Height, Remaining))};
			Remaining = std::max(0.0f, Remaining - Rect.h);
			const float Space = std::min(ui_token::player_card::SECTION_GAP, Remaining);
			Body.y += Rect.h + Space;
			Remaining = std::max(0.0f, Remaining - Space);
			Body.h = Remaining;
			return Rect;
		};
		Layout.m_Header = Take(ui_token::player_card::HEADER_HEIGHT);
		Layout.m_Actions = Take(PlayerCardActionsHeight(ActionCount));
		Layout.m_IconRow = Take(ui_token::player_card::ICON_BUTTON_SIZE);
		Layout.m_Status = Take(ui_token::player_card::STATUS_HEIGHT);
		Layout.m_Details = Body;
		return Layout;
	}

	struct SPlayerCardActionGrid
	{
		std::array<CUIRect, static_cast<int>(EPlayerCardAction::COUNT)> m_aCells;
		int m_Rows = 0;
	};

	// 胶囊操作区的两列网格：末行只剩一个动作时占满整行，不把按钮挤成半宽。
	inline SPlayerCardActionGrid PlayerCardActionGrid(const CUIRect &Actions, const int ActionCount)
	{
		SPlayerCardActionGrid Grid;
		const int Columns = std::max(1, ui_token::player_card::ACTION_COLUMNS);
		Grid.m_Rows = PlayerCardActionRows(ActionCount);
		if(Grid.m_Rows <= 0 || Actions.w <= 0.0f)
			return Grid;
		const int Count = std::min(ActionCount, static_cast<int>(EPlayerCardAction::COUNT));
		const float CellWidth = std::max(0.0f, (Actions.w - (Columns - 1) * ui_token::player_card::ACTION_GAP) / Columns);
		int Index = 0;
		for(int Row = 0; Row < Grid.m_Rows && Index < Count; ++Row)
		{
			const int InRow = std::min(Columns, Count - Index);
			const float Y = Actions.y + Row * (ui_token::player_card::ACTION_HEIGHT + ui_token::player_card::ACTION_GAP);
			for(int Column = 0; Column < InRow; ++Column)
			{
				const float Width = InRow == 1 ? Actions.w : CellWidth;
				const float X = InRow == 1 ? Actions.x : Actions.x + Column * (CellWidth + ui_token::player_card::ACTION_GAP);
				Grid.m_aCells[Index] = CUIRect{X, Y, Width, ui_token::player_card::ACTION_HEIGHT};
				++Index;
			}
		}
		return Grid;
	}

	// 展示区图标行：固定四格（好友 / 静音 / 屏蔽表情 / 跨服跟随）。
	// 行内动作图标条：好友 / 静音 / 屏蔽表情 / 跟随 / 复制，五格足够覆盖玩家页全部动作。
	struct SPlayerShowcaseIconRow
	{
		std::array<CUIRect, 5> m_aCells{};
	};

	struct SPlayerCardIconRow
	{
		std::array<CUIRect, static_cast<int>(EPlayerCardCopy::COUNT)> m_aCells;
	};

	// 图标复制行：小方形按钮靠右排成一行。行宽不足时等比缩小边长，
	// 而不是把最后一个按钮推出行外——命中区永远留在行内。
	// 为什么不用 ui_widget::CIconActionStrip：那套的语义是「不存在 = 不占位」并要求出现/消失
	// 过渡，复制按钮在这里是「恒存在、可能置灰」——置灰仍要占槽，否则三个图标会随玩家数据
	// 左右横跳。等哪天真有「可隐藏」的图标动作，再切到图标条组件。
	inline SPlayerCardIconRow PlayerCardIconRowLayout(const CUIRect &Row, const int IconCount)
	{
		SPlayerCardIconRow Layout;
		const int Count = std::max(0, std::min(IconCount, static_cast<int>(EPlayerCardCopy::COUNT)));
		if(Count <= 0 || Row.w <= 0.0f)
			return Layout;
		const float Gap = ui_token::player_card::ICON_BUTTON_GAP;
		const float TotalGap = (Count - 1) * Gap;
		float Size = ui_token::player_card::ICON_BUTTON_SIZE;
		if(Size * Count + TotalGap > Row.w)
			Size = std::max(0.0f, (Row.w - TotalGap) / Count);
		const float Total = Size * Count + TotalGap;
		float X = Row.x + std::max(0.0f, Row.w - Total);
		const float Y = Row.y + std::max(0.0f, (Row.h - Size) * 0.5f);
		for(int Index = 0; Index < Count; ++Index)
		{
			Layout.m_aCells[Index] = CUIRect{X, Y, Size, Size};
			X += Size + Gap;
		}
		return Layout;
	}

	// 一个操作胶囊的显示事实：文案、是否处于选中态、是否可点。
	struct SPlayerCardActionSpec
	{
		const char *m_pLabel = nullptr;
		bool m_Selected = false;
		bool m_Enabled = true;
	};

	// 胶囊操作区：全部走 ui_widget::SecondaryButton，返回被点中的动作。
	inline EPlayerCardAction PlayerCardActions(const IUiContext &Ctx, CButtonContainer *pButtons, const CUIRect &Actions, const SPlayerCardActionSpec *pSpecs, const int Count)
	{
		if(Ctx.m_pUi == nullptr || pButtons == nullptr || pSpecs == nullptr || Count <= 0)
			return EPlayerCardAction::NONE;
		const auto Grid = PlayerCardActionGrid(Actions, Count);
		const int Clamped = std::min(Count, static_cast<int>(EPlayerCardAction::COUNT));
		for(int Index = 0; Index < Clamped; ++Index)
		{
			const SPlayerCardActionSpec &Spec = pSpecs[Index];
			if(Spec.m_pLabel == nullptr)
				continue;
			if(ui_widget::SecondaryButton(Ctx, &pButtons[Index], Spec.m_pLabel, Grid.m_aCells[Index], !Spec.m_Enabled, EUiButtonGeometry::CONTROL, Spec.m_Selected))
				return static_cast<EPlayerCardAction>(Index);
			ui_widget::Tooltip(Ctx, &pButtons[Index], Grid.m_aCells[Index], Spec.m_pLabel);
		}
		return EPlayerCardAction::NONE;
	}

	// 一个图标复制按钮的显示事实：图标、文案、要说给玩家的提示。
	struct SPlayerCardCopySpec
	{
		EQmIcon m_Icon = EQmIcon::COPY;
		const char *m_pFallbackIcon = nullptr;
		// 点击后写进剪贴板的文本；空文本表示这一项当前不可复制（按钮置灰）。
		const char *m_pText = nullptr;
		const char *m_pTooltip = nullptr;
	};

	// 图标复制区：点一下即复制。非空文本才写剪贴板，空文本不覆盖玩家原有内容。
	// 返回被点中的复制项，调用方据此弹一条轻提示。
	inline EPlayerCardCopy PlayerCardCopies(const IUiContext &Ctx, CButtonContainer *pButtons, const CUIRect &Row, const SPlayerCardCopySpec *pSpecs, const int Count)
	{
		if(Ctx.m_pUi == nullptr || pButtons == nullptr || pSpecs == nullptr || Count <= 0)
			return EPlayerCardCopy::NONE;
		const auto Layout = PlayerCardIconRowLayout(Row, Count);
		const int Clamped = std::min(Count, static_cast<int>(EPlayerCardCopy::COUNT));
		for(int Index = 0; Index < Clamped; ++Index)
		{
			const SPlayerCardCopySpec &Spec = pSpecs[Index];
			const bool Enabled = Spec.m_pText != nullptr && Spec.m_pText[0] != '\0';
			if(ui_widget::DoIconButton(Ctx, &pButtons[Index], Spec.m_Icon, Spec.m_pFallbackIcon, 0, Layout.m_aCells[Index], BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Enabled) != 0)
			{
				if(Enabled && Ctx.m_pUi->Input() != nullptr)
					Ctx.m_pUi->Input()->SetClipboardText(Spec.m_pText);
				return static_cast<EPlayerCardCopy>(Index);
			}
			ui_widget::Tooltip(Ctx, &pButtons[Index], Layout.m_aCells[Index], Spec.m_pTooltip);
		}
		return EPlayerCardCopy::NONE;
	}
	// 玩家列表是网格卡片：列数由可用宽度与格宽推导，窗口变窄自动减列，最少一列。
	inline int PlayerTileColumns(const float AvailableWidth)
	{
		const float Tile = ui_token::player_card::TILE_MIN_WIDTH;
		const float Gap = ui_token::player_card::TILE_GAP;
		if(AvailableWidth <= 0.0f || Tile <= 0.0f)
			return 1;
		return std::max(1, (int)((AvailableWidth + Gap) / (Tile + Gap)));
	}

	// 玩家瓦片：与皮肤列表同一种读法——多列小卡，左边角色头像、中间名字与战队两行、
	// 右侧一排较小的动作图标（好友 / 静音 / 屏蔽表情 / 跟随 / 复制）。
	struct SPlayerTile
	{
		CUIRect m_Card{};
		CUIRect m_Avatar{};
		CUIRect m_Name{};
		CUIRect m_Clan{};
		CUIRect m_Icons{};
	};

	// 瓦片图标条：按瓦片自己的图标档排（比整行卡小一档），从右往左。
	inline SPlayerShowcaseIconRow PlayerTileIconRow(const CUIRect &Row, const int IconCount)
	{
		SPlayerShowcaseIconRow Layout;
		const int Count = std::max(0, std::min(IconCount, static_cast<int>(std::size(Layout.m_aCells))));
		if(Count <= 0 || Row.w <= 0.0f)
			return Layout;
		const float Gap = ui_token::player_card::ICON_BUTTON_GAP;
		float Size = ui_token::player_card::TILE_ICON_SIZE;
		const float TotalGap = (Count - 1) * Gap;
		if(Size * Count + TotalGap > Row.w)
			Size = std::max(0.0f, (Row.w - TotalGap) / (float)Count);
		Size = std::min(Size, Row.h);
		CUIRect Cursor = Row;
		for(int Index = Count - 1; Index >= 0; --Index)
		{
			CUIRect Cell;
			Cursor.VSplitRight(Size, &Cursor, &Cell);
			Cell.h = Size;
			Cell.y = Row.y + (Row.h - Size) * 0.5f;
			Layout.m_aCells[Index] = Cell;
			if(Index > 0)
				Cursor.VSplitRight(std::min(Gap, Cursor.w), &Cursor, nullptr);
		}
		return Layout;
	}

	// 瓦片内排版：内边距后从左取头像、从右取动作图标条，中间两行文字。
	inline SPlayerTile PlayerTileLayout(const CUIRect &Tile, const int IconCount)
	{
		SPlayerTile Result;
		if(Tile.w <= 0.0f || Tile.h <= 0.0f)
			return Result;
		Result.m_Card = Tile;
		CUIRect Content = Tile;
		Content.Margin(ui_token::player_card::PADDING, &Content);

		const int Count = std::max(0, IconCount);
		if(Count > 0)
		{
			const float IconSize = ui_token::player_card::TILE_ICON_SIZE;
			const float IconsWidth = Count * IconSize + (Count - 1) * ui_token::player_card::ICON_BUTTON_GAP;
			Content.VSplitRight(std::min(IconsWidth, Content.w), &Content, &Result.m_Icons);
			Content.VSplitRight(std::min(ui_token::spacing::XS, Content.w), &Content, nullptr);
		}

		const float AvatarSize = std::min(Content.h, ui_token::player_card::TILE_AVATAR_SIZE);
		Content.VSplitLeft(std::min(AvatarSize, Content.w), &Result.m_Avatar, &Content);
		// 头像在瓦片内垂直居中：与右侧名字/战队两行的视觉中线对齐，不贴顶。
		Result.m_Avatar.h = AvatarSize;
		Result.m_Avatar.y = Content.y + (Content.h - AvatarSize) * 0.5f;
		Content.VSplitLeft(std::min(ui_token::spacing::XS, Content.w), nullptr, &Content);

		const float TextBlock = std::min(Content.h, 2.0f * ui_token::player_card::LIST_NAME_HEIGHT);
		Content.HSplitTop(std::min(Content.h, std::max(0.0f, (Content.h - TextBlock) * 0.5f)), nullptr, &Content);
		Content.HSplitTop(std::min(ui_token::player_card::LIST_NAME_HEIGHT, Content.h), &Result.m_Name, &Content);
		Result.m_Clan = Content;
		return Result;
	}

} // namespace QmPlayersUi

#endif
