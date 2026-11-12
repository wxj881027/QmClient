/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "menus.h"

#include <base/system.h>

#include <engine/engine.h>

#include <game/client/QmUi/QmToast.h>
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
	constexpr int SCREENSHOT_GALLERY_COLUMNS = 4;

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
	if(Item.m_DateLoaded && Item.m_DateValid)
		str_timestamp_ex(Item.m_Date, aValue, sizeof(aValue), FORMAT_SPACE);
	else
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

	// 胶囊格：行列表那一套统一行外观（行距、行内退、覆盖式滚动条、主题选中填充）同样用在
	// 4 列网格上。行距必须并进行高：组件在 ItemsPerRow > 1 时按每个格子推进一次自动行距，
	// 4 列网格的行距会变成 4 倍，内容高度少算、行位逐行漂移，底部的图片/文件夹滚不到。
	s_GalleryListBox.SetCapsuleRows(ui_token::font::BODY);
	s_GalleryListBox.DoAutoSpacing(0.0f);
	// 选中填充内缩半档行距：填充贴合卡片（格与格之间那条缝不染色）。
	s_GalleryListBox.SetItemSelectionInset(ui_token::list::ROW_SPACING * 0.5f);
	// 轨道压在内容上：列宽按「行让位」这一份宽度算。RowReserveWidth 由 token 直接算，
	// 不依赖上一帧的滚动条状态；轨道真正出现时组件把列表体整块右缩同一列，格子宽度不跳。
	const float GalleryCellWidth = std::max(0.0f, ListBox.w - s_GalleryListBox.RowReserveWidth()) / (float)SCREENSHOT_GALLERY_COLUMNS;
	const float PreviewHeight = std::clamp(GalleryCellWidth * 0.60f, 72.0f, 150.0f);
	const float RowHeight = PreviewHeight + 42.0f;
	const float RowPitch = RowHeight + ui_token::list::ROW_SPACING;
	const float CardRadius = ui_token::list::RowRadius(RowHeight);
	// 容器内边距：格内容区在容器四边内缩（档位由行高派生），首/末行的卡片不再贴着
	// 容器圆角。可见行数仍按整格高度反推，组件为保行数只把留白收敛到富余的那一段。
	s_GalleryListBox.SetContainerPadding(ui_token::list::ContainerPadding(RowHeight));
	if(m_DemolistSelectedReveal)
	{
		s_GalleryListBox.ScrollToSelected();
		m_DemolistSelectedReveal = false;
	}
	// 整数行：行高即 RowPitch（含行距），组件按 RowsHeight(可见行, RowPitch, 0) = 可见行 × RowPitch
	// 收高度，视图正好停在整格边界，最后一行完整可见而不是半张。空列表没有条目行
	// （组件会把 0 条目收成 0 高，连自绘的空态占位行一起裁掉），传 (0, 0) 关掉自适应高度。
	const int GalleryVisibleRows = m_vpFilteredDemos.empty() ? 0 :
								   (int)std::floor(std::max(0.0f, ListBox.h) / RowPitch);
	s_GalleryListBox.SetVisibleRowRange(GalleryVisibleRows > 0 ? 1 : 0, GalleryVisibleRows);
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
		// 卡片排在这一格的内容区里（左右已含胶囊格的行内退）；纵向夹回行高，
		// RowPitch 里多出来的那一截行距只当格间缝，不撑高卡片。
		CUIRect Card = ListItem.m_ContentRect;
		Card.h = std::min(Card.h, RowHeight);
		CUIRect Shadow = Card;
		Shadow.x += 2.0f;
		Shadow.y += 2.0f;
		Shadow.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.18f), IGraphics::CORNER_ALL, 4.0f);
		// 选中/焦点底色由 CListBox 的胶囊格轨道统一绘制（主题 m_Selected 填充、无描边），
		// 这里不再自绘那层。多选（Ctrl/Shift）组件不跟踪，用同一主题色补在卡片面上，
		// 否则多选中的卡片和没选中的长得一样。
		const ColorRGBA CardColor = Selected && !Focused ?
						    Ui()->ScaleBackgroundAlpha(Ui()->QmControlTheme().m_Selected) :
						    ColorRGBA(1.0f, 1.0f, 1.0f, 0.09f);
		Card.Draw(CardColor, IGraphics::CORNER_ALL, 4.0f);
		Card.Margin(4.0f, &Card);

		CUIRect Preview, Caption;
		Card.HSplitTop(Card.h - 22.0f, &Preview, &Caption);
		Preview.Margin(1.0f, &Preview);
		Preview.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.24f), IGraphics::CORNER_ALL, 2.0f);
		Preview.Margin(3.0f, &Preview);

		if(pItem->m_IsDir)
		{
			const bool ParentFolder = str_comp(pItem->m_aFilename, "..") == 0;
			const EQmIcon Icon = ParentFolder || pItem->m_IsLink ? EQmIcon::FOLDER_TREE : EQmIcon::FOLDER;
			const char *pIcon = ParentFolder || pItem->m_IsLink ? FONT_ICON_FOLDER_TREE : FONT_ICON_FOLDER;
			Ui()->DoLabel_QmIcon(&Preview, Icon, pIcon, std::min(26.0f, Preview.h * 0.34f), TEXTALIGN_MC);
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
			// 其余情况是后台正在解码：保留占位底色，贴图就绪后的帧再画。
		}

		Caption.HMargin(1.0f, &Caption);
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
