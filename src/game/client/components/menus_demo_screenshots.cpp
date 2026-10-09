/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "menus.h"

#include <base/system.h>

#include <engine/engine.h>

#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
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

	CUIRect FitScreenshotImageRect(CUIRect Rect, int Width, int Height)
	{
		const float Scale = std::min(Rect.w / Width, Rect.h / Height);
		return {Rect.x + (Rect.w - Width * Scale) * 0.5f, Rect.y + (Rect.h - Height * Scale) * 0.5f, Width * Scale, Height * Scale};
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

void CMenus::RenderDemoScreenshotPreview(CUIRect PreviewRect, const CDemoItem &Item)
{
	PreviewRect.Margin(3.0f, &PreviewRect);
	PreviewRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.28f), IGraphics::CORNER_ALL, 6.0f);
	PreviewRect.Margin(5.0f, &PreviewRect);

	if(!LoadDemoScreenshotPreviewTexture(Item) || m_DemoScreenshotPreviewWidth <= 0 || m_DemoScreenshotPreviewHeight <= 0)
	{
		Ui()->DoLabel(&PreviewRect, Localize("Could not preview this image"), 12.0f, TEXTALIGN_MC);
		return;
	}

	RenderScreenshotTexture(Graphics(), m_DemoScreenshotPreviewTexture, m_DemoScreenshotPreviewWidth, m_DemoScreenshotPreviewHeight, PreviewRect);
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

	const CUIRect ImageRect = FitScreenshotImageRect(PreviewRect, m_DemoScreenshotPreviewWidth, m_DemoScreenshotPreviewHeight);
	RenderScreenshotTexture(Graphics(), m_DemoScreenshotPreviewTexture, m_DemoScreenshotPreviewWidth, m_DemoScreenshotPreviewHeight, PreviewRect);

	char aSourcePath[IO_MAX_PATH_LENGTH];
	str_format(aSourcePath, sizeof(aSourcePath), "%s/%s", m_aCurrentDemoFolder, Item.m_aFilename);
	const CQmScreenshotManager::SWatermarkOptions Options = CQmScreenshotManager::CurrentWatermarkOptions();
	const std::string Text = m_ScreenshotManager.BuildWatermarkText(Storage(), aSourcePath, Item.m_StorageType, Options);
	if(Text.empty() || ImageRect.w <= 0.0f || ImageRect.h <= 0.0f)
		return;

	const bool Top = Options.m_Position == CQmScreenshotManager::EWatermarkPosition::TOP_LEFT || Options.m_Position == CQmScreenshotManager::EWatermarkPosition::TOP_RIGHT;
	const bool Right = Options.m_Position == CQmScreenshotManager::EWatermarkPosition::BOTTOM_RIGHT || Options.m_Position == CQmScreenshotManager::EWatermarkPosition::TOP_RIGHT;
	CUIRect Band = ImageRect;
	Band.h = std::clamp(ImageRect.h * 0.24f, 18.0f, 34.0f);
	if(!Top)
		Band.y += ImageRect.h - Band.h;
	Band.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.62f), IGraphics::CORNER_NONE, 0.0f);
	Band.Margin(5.0f, &Band);
	SLabelProperties TextProperties;
	TextProperties.m_MaxWidth = Band.w;
	TextProperties.m_EllipsisAtEnd = true;
	TextProperties.m_EnableWidthCheck = false;
	Ui()->DoLabel(&Band, Text.c_str(), std::clamp(Band.h * 0.48f, 8.0f, 13.0f), Right ? TEXTALIGN_MR : TEXTALIGN_ML, TextProperties);
}

void CMenus::RenderDemoScreenshotDetails(CUIRect Contents, const CDemoItem &Item, float FontSize)
{
	const float Gap = 6.0f;
	static CScrollRegion s_DetailsScroll;
	vec2 ScrollOffset;
	s_DetailsScroll.Begin(&Contents, &ScrollOffset);
	Contents.y += ScrollOffset.y;
	const float PreviewHeight = std::clamp(Contents.w * 0.72f, 92.0f, 170.0f);
	const float WatermarkHeight = 18.0f + 5.0f * 24.0f + 14.0f;
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

	qm_card_catalog::SQmCardBuildContext WatermarkCtx;
	WatermarkCtx.m_pMenus = this;
	WatermarkCtx.m_UiContext = SettingsUiContext("screenshot_watermark_settings");
	WatermarkCtx.m_Metrics.m_LineHeight = 22.0f;
	WatermarkCtx.m_Metrics.m_LineSpacing = 2.0f;
	WatermarkCtx.m_Metrics.m_BodySize = FontSize;
	qm_card_catalog::QmCardRenderHook::RenderScreenshotWatermarkSettings(WatermarkCtx, WatermarkCard);

	// 先处理控件，再绘制预览，使开关、文本和位置在同一帧生效。
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
	const bool Busy = m_pScreenshotWatermarkJob != nullptr;
	const bool Clicked = DoButton_Menu_QmIcon(&s_WatermarkButton, Busy ? EQmIcon::CLOSE : EQmIcon::PENCIL, Busy ? FONT_ICON_XMARK : FONT_ICON_PENCIL, Busy ? 1 : 0, &Rect);
	TextRender()->SetRenderFlags(PreviousFlags);
	TextRender()->SetFontPreset(PreviousPreset);
	GameClient()->m_Tooltips.DoToolTip(&s_WatermarkButton, &Rect, Busy ? Localize("Cancel") : Localize("Apply watermark"));
	if(Busy)
	{
		if(Clicked)
			m_pScreenshotWatermarkJob->Cancel();
		return false;
	}
	return Clicked;
}

bool CMenus::ApplyDemoScreenshotWatermark(const CDemoItem &Item)
{
	if(!DemoBrowserBrowsingScreenshots() || Item.m_IsDir ||
		(str_endswith_nocase(Item.m_aFilename, ".png") == nullptr && str_endswith_nocase(Item.m_aFilename, ".webp") == nullptr))
		return false;

	char aSourcePath[IO_MAX_PATH_LENGTH];
	str_format(aSourcePath, sizeof(aSourcePath), "%s/%s", m_aCurrentDemoFolder, Item.m_aFilename);
	char aTargetPath[IO_MAX_PATH_LENGTH];
	str_copy(aTargetPath, aSourcePath);
	char *pExtension = strrchr(aTargetPath, '.');
	if(pExtension == nullptr)
		pExtension = aTargetPath + str_length(aTargetPath);
	str_copy(pExtension, "_watermarked.png", sizeof(aTargetPath) - (pExtension - aTargetPath));

	const CQmScreenshotManager::SWatermarkOptions Options = CQmScreenshotManager::CurrentWatermarkOptions();
	if(m_pScreenshotWatermarkJob != nullptr)
		return false;
	m_pScreenshotWatermarkJob = m_ScreenshotManager.CreateWatermarkJob(Storage(), aSourcePath, Item.m_StorageType, aTargetPath, Options);
	if(m_pScreenshotWatermarkJob == nullptr)
		return false;
	m_ScreenshotWatermarkFolder = m_aCurrentDemoFolder;
	Engine()->AddJob(m_pScreenshotWatermarkJob);
	return true;
}

void CMenus::PumpDemoScreenshotWatermark()
{
	if(m_pScreenshotWatermarkJob == nullptr)
		return;
	const bool InGame = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	const bool OnSourcePage = IsActive() && (InGame ? m_GamePage : m_MenuPage) == PAGE_DEMOS &&
				  DemoBrowserBrowsingScreenshots() && m_ScreenshotWatermarkFolder == m_aCurrentDemoFolder;
	if(!OnSourcePage)
		m_pScreenshotWatermarkJob->Cancel();
	if(m_pScreenshotWatermarkJob->State() != IJob::STATE_DONE)
		return;
	if(m_pScreenshotWatermarkJob->Canceled())
	{
		m_pScreenshotWatermarkJob.reset();
		return;
	}
	// 不覆盖正在显示的其它弹窗，结果等到页面仍有效且弹窗空闲再发布。
	if(m_Popup != POPUP_NONE)
		return;
	const bool Saved = m_pScreenshotWatermarkJob->Saved();
	m_pScreenshotWatermarkJob.reset();
	if(Saved)
	{
		m_DemoScreenshotPreviewLoadFailed = false;
		ResetDemoScreenshotPreview();
		DemolistPopulate();
		DemolistOnUpdate(false);
		PopupMessage(Localize("Screenshot saved"), Localize("The watermarked screenshot was saved next to the original"), Localize("Ok"));
	}
	else
		PopupMessage(Localize("Screenshot error"), Localize("Unable to save the watermarked screenshot"), Localize("Ok"));
}

void CMenus::RenderDemoScreenshotGallery(CUIRect ListBox, bool &WasListboxItemActivated)
{
	static CListBox s_GalleryListBox;
	static std::vector<int> s_vGalleryItemIds;
	s_vGalleryItemIds.resize(m_vpFilteredDemos.size());
	for(size_t Index = 0; Index < s_vGalleryItemIds.size(); ++Index)
		s_vGalleryItemIds[Index] = (int)Index;

	const float CellWidth = ListBox.w / (float)SCREENSHOT_GALLERY_COLUMNS;
	const float PreviewHeight = std::clamp(CellWidth * 0.60f, 72.0f, 150.0f);
	const float RowHeight = PreviewHeight + 42.0f;
	s_GalleryListBox.SetScrollbarAlwaysReserved(true);
	s_GalleryListBox.DoAutoSpacing(4.0f);
	if(m_DemolistSelectedReveal)
	{
		s_GalleryListBox.ScrollToSelected();
		m_DemolistSelectedReveal = false;
	}
	s_GalleryListBox.DoStart(RowHeight, (int)m_vpFilteredDemos.size(), SCREENSHOT_GALLERY_COLUMNS, 1, m_DemolistSelectedIndex, &ListBox, false, IGraphics::CORNER_B);

	int ItemIndex = -1;
	int VisibleRows = 0;
	int FirstVisibleIndex = -1;
	int EndVisibleIndex = -1;
	for(const auto *pItem : m_vpFilteredDemos)
	{
		++ItemIndex;
		const bool Focused = ItemIndex == m_DemolistSelectedIndex;
		const bool Selected = IsDemoItemSelected(*pItem);
		const CListboxItem ListItem = s_GalleryListBox.DoNextItem(&s_vGalleryItemIds[ItemIndex], Focused, ui_token::radius::BASE);
		if(!ListItem.m_Visible)
			continue;

		auto GaussianBlurSuppression = ListItem.SuppressGaussianBlur();
		++VisibleRows;
		if(FirstVisibleIndex < 0)
			FirstVisibleIndex = ItemIndex;
		EndVisibleIndex = ItemIndex + 1;
		CUIRect Card = ListItem.m_Rect;
		Card.Margin(4.0f, &Card);
		CUIRect Shadow = Card;
		Shadow.x += 2.0f;
		Shadow.y += 2.0f;
		Shadow.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.18f), IGraphics::CORNER_ALL, 4.0f);
		const ColorRGBA CardColor = Focused ? ui_token::color::ACCENT_PRIMARY_DIM.WithAlpha(0.30f) :
						      (Selected ? ui_token::color::ACCENT_PRIMARY_DIM.WithAlpha(0.20f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.09f));
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

	for(const CDemoItem *pItem : m_vpFilteredDemos)
	{
		if(!IsDemoScreenshotPreviewItem(*pItem))
			continue;
		const float PreviewRowHeight = std::clamp(ListBox.w * 0.36f, 140.0f, 260.0f);
		const CListboxItem PreviewItem = s_GalleryListBox.DoCustomRow(PreviewRowHeight, true);
		if(PreviewItem.m_Visible)
			RenderDemoScreenshotPreview(PreviewItem.m_Rect, *pItem);
		break;
	}

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
	if(WasListboxItemActivated && IsValidDemoIndex(m_DemolistSelectedIndex) && !m_vpFilteredDemos[m_DemolistSelectedIndex]->m_IsDir)
	{
		ToggleDemoScreenshotPreview(*m_vpFilteredDemos[m_DemolistSelectedIndex]);
		// 双击文件由网格打开内嵌大图，避免按钮栏再次把它交给系统外部查看器。
		WasListboxItemActivated = false;
	}

	if(g_Config.m_BrDemoSort == SORT_DATE)
	{
		const bool PreviousMetadataBackgroundAllowed = m_DemoBrowserMetadataBackgroundAllowed;
		m_DemoBrowserMetadataBackgroundAllowed = !s_GalleryListBox.ScrollbarActive() && !s_GalleryListBox.ScrollbarAnimating();
		AdvanceDemoBrowserMetadata(0, std::max(1, VisibleRows), "gallery", FirstVisibleIndex, EndVisibleIndex);
		m_DemoBrowserMetadataBackgroundAllowed = PreviousMetadataBackgroundAllowed;
	}
}
