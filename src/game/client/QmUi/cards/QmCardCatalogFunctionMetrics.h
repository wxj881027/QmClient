#ifndef GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGFUNCTIONMETRICS_H
#define GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGFUNCTIONMETRICS_H

#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/textrender.h>

#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/pie_menu_logic.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cstddef>

// 函数分类卡片的文本高度及旧版特性行定义。
namespace qm_card_catalog
{
	// 饼菜单卡片高度：与卡片里的实际布局同一份算式——固定 8 行控件、一个全宽预览容器，
	// 再加两个选项池（各含一行标题与一行胶囊；池内换行会让实际高度更高，卡片自己会再量）。
	inline float QmPieMenuContentHeight(float ContentWidth, float LineHeight, float BodySize, float LineSpacing, bool Enabled, bool Following)
	{
		const float Row = LineHeight + LineSpacing;
		const float FollowHeight = Following ? Row : 0.0f;
		if(!Enabled)
			return Row + FollowHeight;
		const float PreviewSide = std::clamp(ContentWidth * 0.42f, LineHeight * 8.0f, LineHeight * 12.0f);
		const float PoolPadding = ui_token::list::ContainerPadding(LineHeight);
		const float PreviewBlock = PreviewSide + PoolPadding * 2.0f + LineSpacing;
		const float PoolBlock = (BodySize + LineSpacing) + (PoolPadding * 2.0f + LineHeight) + LineSpacing;
		return Row * 8.0f + FollowHeight + PreviewBlock + PoolBlock * 2.0f;
	}

	// 保留旧版特性文案的翻译提取标注；本地渲染逐项绘制，行数以其实际控件为准。
	struct SQmMiniFeatureRow
	{
		const void *m_pId;
		const char *m_pTextId;
		int *m_pValue;
	};

	inline const std::array<SQmMiniFeatureRow, 12> &QmMiniFeatureRows()
	{
		// 行文案以 Localizable 标注：表在卡片目录里，渲染侧只做 Localize(m_pTextId)，
		// 不加标注翻译提取脚本就看不到这些 source key，语言文件会整行退回英文。
		// IME 设置已移到独立卡片；本表只保留小功能卡片自己的开关。
		// 本地差异：远程此表含 g_Config.m_QmProcessHighPriority（"High process priority"），
		// 该项为本地既定不吸收（本地合同测试断言其不存在），故整行删除且表长保持为实际行数。
		static const std::array<SQmMiniFeatureRow, 12> s_aRows = {{
			{&g_Config.m_QmClientShowBadge, Localizable("Show Qm badge"), &g_Config.m_QmClientShowBadge},
			{&g_Config.m_QmAutoUpdate, Localizable("Automatic updates"), &g_Config.m_QmAutoUpdate},
			{&g_Config.m_QmShowOutdatedVersionWarning, Localizable("Show outdated version warning"), &g_Config.m_QmShowOutdatedVersionWarning},
			{&g_Config.m_QmHideJoinServerInfo, Localizable("Hide server information on join"), &g_Config.m_QmHideJoinServerInfo},
			{&g_Config.m_QmMessageMerge, Localizable("Message merging"), &g_Config.m_QmMessageMerge},
			{&g_Config.m_QmShortServerNames, Localizable("Short server names"), &g_Config.m_QmShortServerNames},
			{&g_Config.m_QmRepeatEnabled, Localizable("Enable repeat"), &g_Config.m_QmRepeatEnabled},
			{&g_Config.m_QmHumming, Localizable("Humming"), &g_Config.m_QmHumming},
			{&g_Config.m_QmSinging, Localizable("Singing"), &g_Config.m_QmSinging},
			{&g_Config.m_QmRandomEmoteOnHit, Localizable("Random emoticon"), &g_Config.m_QmRandomEmoteOnHit},
			{&g_Config.m_QmComboPopup, Localizable("Combo"), &g_Config.m_QmComboPopup},
			{&g_Config.m_QmSayNoPop, Localizable("Hide input emoticon"), &g_Config.m_QmSayNoPop},
		}};
		return s_aRows;
	}

	// 旧版特性行定义的附加行数。
	inline constexpr size_t QmMiniFeatureSpecialRowCount = 2;

	// 多行输入框（词条过滤 / 关键词回复）按文本换行数估算高度。
	inline float CalcQiaFenInputHeight(ITextRender *pTextRender, const char *pText, const float Width, const float TextFontSize, const float LineSpacing, const float MinHeight)
	{
		const float VPadding = 2.0f;
		const float LineWidth = std::max(1.0f, Width - VPadding * 2.0f);
		const char *pMeasureText = pText != nullptr && pText[0] != '\0' ? pText : " ";
		const float Height = pTextRender->TextBoundingBox(TextFontSize, pMeasureText, -1, LineWidth, LineSpacing).m_H;
		return std::max(MinHeight, Height + VPadding * 2.0f);
	}

	// 地图上传卡片的三行说明文案与其换行高度。
	inline std::array<const char *, 3> QmMapUploadInstructions()
	{
		return {Localize("Upload .map files. Server and admin passwords: s."),
			Localize("Press F2, log in, then use: change_map map_name"),
			Localize("After uploading again, use: reload or hot_reload")};
	}

	inline float QmMapUploadHelpLineHeight(ITextRender *pTextRender, const char *pText, const float Width, const float BodySize, const float LineHeight)
	{
		return std::max(LineHeight, pTextRender->TextBoundingBox(BodySize, pText, -1, std::max(1.0f, Width)).m_H + BodySize * 0.25f);
	}
} // namespace qm_card_catalog

#endif // GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGFUNCTIONMETRICS_H
