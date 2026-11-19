/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "menus.h"

#include <base/system.h>

#include <engine/engine.h>

#include <game/client/QmUi/QmToast.h>
#include <game/client/components/qmclient/browser_status_layout.h>
#include <game/client/components/qmclient/screenshot_viewer.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

using namespace FontIcons;

namespace
{
	// 列数固定：卡片宽度 = 面板宽 / 列数 − 格间距（见 ui_token::list::GRID_SPACING），
	// 窗口变宽时卡片等比变大。5 列是实机观感档：4 列在大窗口里卡片偏大。
	constexpr int SCREENSHOT_GALLERY_COLUMNS = 5;
	// 卡片底部说明行高度（文件名一行）。
	constexpr float SCREENSHOT_GALLERY_CAPTION_HEIGHT = 22.0f;
	// 预览图宽高比（高 / 宽）：卡片尺寸只跟列宽走，窗口变宽卡片等比变大。
	constexpr float SCREENSHOT_GALLERY_PREVIEW_ASPECT = 0.60f;

	void RenderScreenshotTexture(IGraphics *pGraphics, const CQmScreenshotManager::SThumbnail &Thumbnail, CUIRect Rect)
	{
		if(!Thumbnail.m_Texture.IsValid() || Thumbnail.m_Width <= 0 || Thumbnail.m_Height <= 0)
			return;

		const float Scale = std::min(Rect.w / Thumbnail.m_Width, Rect.h / Thumbnail.m_Height);
		CUIRect ImageRect;
		ImageRect.w = Thumbnail.m_Width * Scale;
		ImageRect.h = Thumbnail.m_Height * Scale;
		ImageRect.x = Rect.x + (Rect.w - ImageRect.w) * 0.5f;
		ImageRect.y = Rect.y + (Rect.h - ImageRect.h) * 0.5f;

		pGraphics->TextureSet(Thumbnail.m_Texture);
		pGraphics->WrapClamp();
		pGraphics->QuadsBegin();
		pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		IGraphics::CQuadItem QuadItem(ImageRect.x, ImageRect.y, ImageRect.w, ImageRect.h);
		pGraphics->QuadsDrawTL(&QuadItem, 1);
		pGraphics->QuadsEnd();
		pGraphics->WrapNormal();
	}

	void RenderScreenshotTexture(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, int Width, int Height, CUIRect Rect)
	{
		if(!Texture.IsValid() || Width <= 0 || Height <= 0)
			return;

		const float Scale = std::min(Rect.w / Width, Rect.h / Height);
		CUIRect ImageRect;
		ImageRect.w = Width * Scale;
		ImageRect.h = Height * Scale;
		ImageRect.x = Rect.x + (Rect.w - ImageRect.w) * 0.5f;
		ImageRect.y = Rect.y + (Rect.h - ImageRect.h) * 0.5f;

		pGraphics->TextureSet(Texture);
		pGraphics->WrapClamp();
		pGraphics->QuadsBegin();
		pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		IGraphics::CQuadItem QuadItem(ImageRect.x, ImageRect.y, ImageRect.w, ImageRect.h);
		pGraphics->QuadsDrawTL(&QuadItem, 1);
		pGraphics->QuadsEnd();
		pGraphics->WrapNormal();
	}

	void FormatScreenshotFileSize(int64_t SizeBytes, char *pBuf, size_t BufSize)
	{
		const float SizeKiB = SizeBytes / 1024.0f;
		if(SizeKiB > 1024.0f)
			str_format(pBuf, BufSize, Localize("%.2f MiB"), SizeKiB / 1024.0f);
		else
			str_format(pBuf, BufSize, Localize("%.2f KiB"), SizeKiB);
	}

}

void CMenus::OpenDemoScreenshotViewer(const CDemoItem &Item)
{
	if(!DemoBrowserBrowsingScreenshots() || Item.m_IsDir)
		return;
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s", m_aCurrentDemoFolder, Item.m_aFilename);
	char aFullPath[IO_MAX_PATH_LENGTH];
	Storage()->GetCompletePath(Item.m_StorageType, aPath, aFullPath, sizeof(aFullPath));
	// 外部查看器起不来只影响这一次点击，再点一次就是重试：用非阻断失败提示。
	if(!OpenQmScreenshotViewer(Client(), aFullPath, Localize("Error opening screenshot"), Localize("Unable to open the screenshot file")))
		qm_ui::Toast(EQmToastKind::FAILED, Localize("Unable to open the screenshot file"));
}

void CMenus::RenderDemoScreenshotWatermarkPreview(CUIRect PreviewRect, const CDemoItem &Item)
{
	PreviewRect.Margin(3.0f, &PreviewRect);
	PreviewRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.20f), IGraphics::CORNER_ALL, 6.0f);
	PreviewRect.Margin(5.0f, &PreviewRect);

	if(!LoadDemoScreenshotPreviewTexture(Item) || m_DemoScreenshotPreviewWidth <= 0 || m_DemoScreenshotPreviewHeight <= 0)
	{
		Ui()->DoLabel(&PreviewRect, Localize("Could not preview this image"), 11.0f, TEXTALIGN_MC);
		return;
	}

	RenderScreenshotTexture(Graphics(), m_DemoScreenshotPreviewTexture, m_DemoScreenshotPreviewWidth, m_DemoScreenshotPreviewHeight, PreviewRect);
}

void CMenus::RenderDemoScreenshotDetails(CUIRect Contents, const CDemoItem &Item, float FontSize)
{
	const float Gap = 6.0f;
	static CScrollRegion s_DetailsScroll;
	vec2 ScrollOffset;
	s_DetailsScroll.Begin(&Contents, &ScrollOffset);
	Contents.y += ScrollOffset.y;
	const float PreviewHeight = std::clamp(Contents.w * 0.72f, 92.0f, 170.0f);
	const float WatermarkHeight = 74.0f;
	Contents.h = PreviewHeight + 72.0f + 2.0f * Gap + WatermarkHeight;
	s_DetailsScroll.AddRect(Contents);
	CUIRect PreviewCard;
	Contents.HSplitTop(std::min(PreviewHeight, Contents.h), &PreviewCard, &Contents);
	PreviewCard.Draw(MenuPanelElevatedColor(), IGraphics::CORNER_ALL, ui_token::radius::BASE);
	PreviewCard.Margin(4.0f, &PreviewCard);

	Contents.HSplitTop(Gap, nullptr, &Contents);

	const float MetadataHeight = std::min(72.0f, Contents.h);
	CUIRect MetadataCard;
	Contents.HSplitTop(MetadataHeight, &MetadataCard, &Contents);
	MetadataCard.Draw(MenuPanelColor(0.62f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
	MetadataCard.Margin(7.0f, &MetadataCard);
	CUIRect Row, Left, Right;
	MetadataCard.HSplitTop(15.0f, &Row, &MetadataCard);
	Ui()->DoLabel(&Row, Localize("File information"), 10.0f, TEXTALIGN_ML);
	MetadataCard.HSplitTop(16.0f, &Row, &MetadataCard);
	Row.VSplitMid(&Left, &Right);
	Ui()->DoLabel(&Left, Localize("Created"), 10.0f, TEXTALIGN_ML);
	Ui()->DoLabel(&Right, Localize("Size"), 10.0f, TEXTALIGN_ML);
	MetadataCard.HSplitTop(22.0f, &Row, &MetadataCard);
	Row.VSplitMid(&Left, &Right);
	char aValue[256];
	if(!Item.m_DateLoaded || !Item.m_DateValid || !str_timestamp_ex_try(Item.m_Date, aValue, sizeof(aValue), FORMAT_SPACE))
		str_copy(aValue, "-");
	SLabelProperties ValueProperties;
	ValueProperties.m_MaxWidth = Left.w;
	ValueProperties.m_EllipsisAtEnd = true;
	ValueProperties.m_EnableWidthCheck = false;
	Ui()->DoLabel(&Left, aValue, 9.0f, TEXTALIGN_ML, ValueProperties);
	if(Item.m_SizeLoaded)
		FormatScreenshotFileSize(Item.m_Size, aValue, sizeof(aValue));
	else
		str_copy(aValue, "-");
	Ui()->DoLabel(&Right, aValue, 9.0f, TEXTALIGN_ML, ValueProperties);

	Contents.HSplitTop(Gap, nullptr, &Contents);
	CUIRect WatermarkCard;
	Contents.HSplitTop(WatermarkHeight, &WatermarkCard, &Contents);
	WatermarkCard.Draw(MenuPanelColor(0.72f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
	WatermarkCard.Margin(7.0f, &WatermarkCard);
	WatermarkCard.HSplitTop(18.0f, &Row, &WatermarkCard);
	Ui()->DoLabel(&Row, Localize("Watermark"), FontSize, TEXTALIGN_ML);

	WatermarkCard.HSplitTop(8.0f, nullptr, &WatermarkCard);
	WatermarkCard.HSplitTop(26.0f, &Row, &WatermarkCard);
	static CButtonContainer s_EditButton;
	if(DoButton_Menu(&s_EditButton, Localize("Watermark editor"), 0, &Row))
		ApplyDemoScreenshotWatermark(Item);

	// 侧栏保留原图预览，编辑操作使用独立的大图工作区。
	RenderDemoScreenshotWatermarkPreview(PreviewCard, Item);
	s_DetailsScroll.End();
}

bool CMenus::DoDemoScreenshotWatermarkButton(const CUIRect &Rect)
{
	static CButtonContainer s_WatermarkButton;
	const EFontPreset PreviousPreset = TextRender()->GetFontPreset();
	const unsigned PreviousFlags = TextRender()->GetRenderFlags();
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	const bool Clicked = DoButton_Menu_QmIcon(&s_WatermarkButton, EQmIcon::PENCIL, FONT_ICON_PENCIL, 0, &Rect);
	TextRender()->SetRenderFlags(PreviousFlags);
	TextRender()->SetFontPreset(PreviousPreset);
	GameClient()->m_Tooltips.DoToolTip(&s_WatermarkButton, &Rect, Localize("Watermark editor"));
	return Clicked;
}

bool CMenus::ApplyDemoScreenshotWatermark(const CDemoItem &Item)
{
	if(!DemoBrowserBrowsingScreenshots() || Item.m_IsDir ||
		(str_endswith_nocase(Item.m_aFilename, ".png") == nullptr && str_endswith_nocase(Item.m_aFilename, ".webp") == nullptr))
		return false;
	char aSourcePath[IO_MAX_PATH_LENGTH];
	str_format(aSourcePath, sizeof(aSourcePath), "%s/%s", m_aCurrentDemoFolder, Item.m_aFilename);
	m_ScreenshotEditor.Open(this, aSourcePath, Item.m_StorageType);
	return true;
}

void CMenus::PumpDemoScreenshotWatermark()
{
	if(!m_ScreenshotEditor.IsOpen())
		return;
	const bool InGame = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	if(!IsActive() || (InGame ? m_GamePage : m_MenuPage) != PAGE_DEMOS || !DemoBrowserBrowsingScreenshots())
		m_ScreenshotEditor.Close(this);
}

void CMenus::RenderDemoScreenshotGallery(CUIRect ListBox, bool &WasListboxItemActivated)
{
	static CListBox s_GalleryListBox;
	static std::vector<int> s_vGalleryItemIds;
	s_vGalleryItemIds.resize(m_vpFilteredDemos.size());
	for(size_t Index = 0; Index < s_vGalleryItemIds.size(); ++Index)
		s_vGalleryItemIds[Index] = (int)Index;

	// 胶囊格：行列表那一套统一行外观（行内退、覆盖式滚动条、主题选中填充）同样用在
	// 多列网格上。自动行距必须关掉：组件在 ItemsPerRow > 1 时按每个格子推进一次，
	// 网格行距会变成列数倍，内容高度少算、行位逐行漂移，底部的图片/文件夹滚不到。
	s_GalleryListBox.SetCapsuleRows(ui_token::font::BODY);
	s_GalleryListBox.DoAutoSpacing(0.0f);
	// 卡片就是反馈矩形：格子里还含行距与行内退，按格子画反馈会与卡面错开一圈
	// （实机反馈「选中底衬和卡片对不上」）。所以内缩归零，反馈延后到卡片矩形画。
	s_GalleryListBox.SetItemSelectionInset(0.0f);
	s_GalleryListBox.SetDeferredItemFeedback(true);
	// 容器永远铺满面板：行数不足或最后一行只剩半行时都不收高度，
	// 否则列表缩到内容高度、面板下方留一条空档（两种实机反馈都撞上过）。
	s_GalleryListBox.SetFillViewportHeight(true);
	// 轨道压在内容上：列宽按「行让位」这一份宽度算。RowReserveWidth 由 token 直接算，
	// 不依赖上一帧的滚动条状态；轨道真正出现时组件把列表体整块右缩同一列，格子宽度不跳。
	const float GalleryCellWidth = std::max(0.0f, ListBox.w - s_GalleryListBox.RowReserveWidth()) / (float)SCREENSHOT_GALLERY_COLUMNS;
	// 卡片尺寸只由列宽决定：预览图按固定宽高比、说明行固定高，两者与面板高度无关。
	// 面板高度只决定这一屏放得下几行——末行露出一截是滚动列表的正常形态，滚动条
	// 负责剩下的内容。绝不反过来拉伸卡片去凑满高度（那样一屏反而只放得下两行）。
	// 格间距取列表的网格档（不是行胶囊那半档），卡片之间才有读得出来的缝。
	const float GallerySpacing = ui_token::list::GRID_SPACING;
	const float RowHeight = QmResolveGalleryRowHeight(GalleryCellWidth, SCREENSHOT_GALLERY_CAPTION_HEIGHT, SCREENSHOT_GALLERY_PREVIEW_ASPECT);
	const float RowPitch = RowHeight + GallerySpacing;
	const float GalleryPadding = ui_token::list::ContainerPadding(RowHeight);
	// 向上取整：末行只露出一部分也要算进可视范围，否则滚动条少算一行、
	// 最后一行永远滚不到底。
	const float GalleryUsableHeight = std::max(0.0f, ListBox.h - 2.0f * GalleryPadding);
	const int GalleryVisibleRows = std::max(1, (int)std::ceil((GalleryUsableHeight + GallerySpacing) / RowPitch));
	const float CardRadius = ui_token::radius::BASE;
	// 容器内边距：格内容区在容器四边内缩（档位由行高派生），首/末行的卡片不再贴着
	// 容器圆角。
	s_GalleryListBox.SetContainerPadding(GalleryPadding);
	if(m_DemolistSelectedReveal)
	{
		s_GalleryListBox.ScrollToSelected();
		m_DemolistSelectedReveal = false;
	}
	// 行高即 RowPitch（含行距）：可见行按 RowHeight 均分可用高度，正好铺满面板；
	// 超出可见行的内容留给滚动条。空列表也保留完整高度（SetFillViewportHeight），
	// 「No screenshots」占位行画在面板里。
	s_GalleryListBox.SetVisibleRowRange(m_vpFilteredDemos.empty() ? 0 : 1, GalleryVisibleRows);
	s_GalleryListBox.DoStart(RowPitch, (int)m_vpFilteredDemos.size(), SCREENSHOT_GALLERY_COLUMNS, 1, m_DemolistSelectedIndex, &ListBox, false, IGraphics::CORNER_B);

	int ItemIndex = -1;
	int VisibleRows = 0;
	int FirstVisibleIndex = -1;
	int EndVisibleIndex = -1;
	for(const auto *pItem : m_vpFilteredDemos)
	{
		++ItemIndex;
		const bool Focused = ItemIndex == m_DemolistSelectedIndex;
		const bool Selected = IsDemoItemSelected(*pItem);
		const CListboxItem ListItem = s_GalleryListBox.DoNextItem(&s_vGalleryItemIds[ItemIndex], Focused, CardRadius);
		if(!ListItem.m_Visible)
			continue;

		auto GaussianBlurSuppression = ListItem.SuppressGaussianBlur();
		++VisibleRows;
		if(FirstVisibleIndex < 0)
			FirstVisibleIndex = ItemIndex;
		EndVisibleIndex = ItemIndex + 1;
		// 卡片：格子去掉右侧与底部的缝，行高夹回 RowHeight。反馈与卡面共用这一个矩形
		// （格子里含行距与行内退，按格子画反馈会大一圈）。
		CUIRect Card = ListItem.m_Rect;
		Card.h = std::min(Card.h, RowHeight);
		Card.x += GallerySpacing * 0.5f;
		Card.w = std::max(0.0f, Card.w - GallerySpacing);
		s_GalleryListBox.DrawItemFeedback(ItemIndex, Card, CardRadius);
		// 一层卡片：上半缩略图 + 底部一行说明。以前是「卡片 + 内层暗底板 + 两层内边距 +
		// 阴影」四层嵌套，缩略图看着像被塞进一个小框里（实机反馈「嵌套太多」）。
		const ColorRGBA CardColor = Selected && !Focused ?
						    Ui()->ScaleBackgroundAlpha(Ui()->QmControlTheme().m_Selected) :
						    ColorRGBA(1.0f, 1.0f, 1.0f, 0.09f);
		Card.Draw(CardColor, IGraphics::CORNER_ALL, CardRadius);

		CUIRect Preview, Caption;
		Card.HSplitTop(std::max(0.0f, Card.h - SCREENSHOT_GALLERY_CAPTION_HEIGHT), &Preview, &Caption);
		Preview.Margin(3.0f, &Preview);

		if(pItem->m_IsDir)
		{
			const bool ParentFolder = str_comp(pItem->m_aFilename, "..") == 0;
			const EQmIcon Icon = ParentFolder || pItem->m_IsLink ? EQmIcon::FOLDER_TREE : EQmIcon::FOLDER;
			const char *pIcon = ParentFolder || pItem->m_IsLink ? FONT_ICON_FOLDER_TREE : FONT_ICON_FOLDER;
			// 图标占预览区高度的一档，再按墨迹高反算字号：先定视觉高度，各字形自己
			// 换算，避免同字号并排时一大一小。旧实现把字号卡在 26，大卡片里图标显得
			// 又小又空（实机反馈「图标可以大一些」）。
			const float IconInkHeight = std::clamp(Preview.h * 0.34f, 18.0f, 64.0f);
			Ui()->DoLabel_QmIcon(&Preview, Icon, pIcon, QmIconInkFontSize(Icon, IconInkHeight), TEXTALIGN_MC);
		}
		else
		{
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "%s/%s", m_aCurrentDemoFolder, pItem->m_aFilename);
			const CQmScreenshotManager::SThumbnail *pThumbnail = m_ScreenshotManager.LoadThumbnail(aPath, pItem->m_StorageType);
			if(pThumbnail != nullptr && pThumbnail->m_Texture.IsValid())
			{
				RenderScreenshotTexture(Graphics(), *pThumbnail, Preview);
			}
			else if(pThumbnail != nullptr && pThumbnail->m_LoadFailed)
			{
				Ui()->DoLabel(&Preview, Localize("Could not preview this image"), 10.0f, TEXTALIGN_MC);
			}
			// 其余情况是后台正在解码：保留卡片底色，贴图就绪后的帧再画。
		}

		SLabelProperties LabelProperties;
		LabelProperties.m_MaxWidth = Caption.w;
		LabelProperties.m_EllipsisAtEnd = true;
		LabelProperties.m_EnableWidthCheck = false;
		Ui()->DoLabel(&Caption, pItem->m_aName, 11.0f, TEXTALIGN_MC, LabelProperties);
	}

	// 可见项已全部登记：本帧只在预算内回收结果并上传缩略图，解码全部发生在后台线程。
	m_ScreenshotManager.PumpThumbnails(Graphics(), Storage(), Engine(), GameClient()->GpuUploadLimiter());

	if(m_vpFilteredDemos.empty())
	{
		const CListboxItem EmptyItem = s_GalleryListBox.DoCustomRow(RowHeight, false);
		if(EmptyItem.m_Visible)
			Ui()->DoLabel(&EmptyItem.m_Rect, Localize("No screenshots"), 12.0f, TEXTALIGN_MC);
	}

	const int OldSelected = m_DemolistSelectedIndex;
	const bool WasItemSelected = s_GalleryListBox.WasItemSelected();
	const int NewSelected = s_GalleryListBox.DoEnd();
	if(WasItemSelected && NewSelected >= 0)
	{
		if(Input()->ShiftIsPressed())
		{
			const int Anchor = m_DemoSelectionAnchorIndex >= 0 ? m_DemoSelectionAnchorIndex : (OldSelected >= 0 ? OldSelected : NewSelected);
			SelectDemoRange(Anchor, NewSelected, Input()->ModifierIsPressed());
			if(m_DemoSelectionAnchorIndex < 0)
				m_DemoSelectionAnchorIndex = Anchor;
		}
		else if(Input()->ModifierIsPressed())
		{
			ToggleDemoSelection(NewSelected);
		}
		else
		{
			SetDemoSelectionSingle(NewSelected);
		}
	}
	else if(NewSelected != OldSelected)
	{
		SetDemoSelectionSingle(NewSelected);
	}

	WasListboxItemActivated = s_GalleryListBox.WasItemActivated() && NumSelectedDemos() == 1;
	// 双击、Enter 和打开按钮统一由按钮栏分发到独立看图窗口。

	if(g_Config.m_BrDemoSort == SORT_DATE)
	{
		const bool PreviousMetadataBackgroundAllowed = m_DemoBrowserMetadataBackgroundAllowed;
		m_DemoBrowserMetadataBackgroundAllowed = !s_GalleryListBox.ScrollbarActive() && !s_GalleryListBox.ScrollbarAnimating();
		AdvanceDemoBrowserMetadata(0, std::max(1, VisibleRows), "gallery", FirstVisibleIndex, EndVisibleIndex);
		m_DemoBrowserMetadataBackgroundAllowed = PreviousMetadataBackgroundAllowed;
	}
}
