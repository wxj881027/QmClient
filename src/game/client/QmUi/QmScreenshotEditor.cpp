#include "QmScreenshotEditor.h"

#include <base/time.h>

#include <engine/engine.h>

#include <game/client/QmUi/QmToast.h>
#include <game/client/components/menus.h>
#include <game/localization.h>

#include <SDL3/SDL_dialog.h>

#include <algorithm>
#include <mutex>

struct CQmScreenshotEditor::SLogoDialog
{
	std::mutex m_Mutex;
	bool m_Done = false;
	bool m_Failed = false;
	std::string m_Path;
};

void CQmScreenshotEditor::ChooseLogo()
{
	if(m_pDialog)
		return;
	m_pDialog = std::make_shared<SLogoDialog>();
	// 回调可能在其他线程发生，也可能晚于编辑器关闭；不捕获菜单或图形对象。
	auto *pOwner = new std::shared_ptr<SLogoDialog>(m_pDialog);
	static const SDL_DialogFileFilter s_aFilters[] = {{"PNG/WEBP", "png;webp"}};
	SDL_ShowOpenFileDialog([](void *pData, const char *const *ppFiles, int) {
		std::unique_ptr<std::shared_ptr<SLogoDialog>> pLifetime(static_cast<std::shared_ptr<SLogoDialog> *>(pData));
		auto &Result = **pLifetime;
		const std::lock_guard<std::mutex> Lock(Result.m_Mutex);
		Result.m_Failed = ppFiles == nullptr;
		if(ppFiles && ppFiles[0])
			Result.m_Path = ppFiles[0];
		Result.m_Done = true;
	},
		pOwner, nullptr, s_aFilters, 1, nullptr, false);
}

void CQmScreenshotEditor::Open(CMenus *pMenus, const char *pPath, int StorageType)
{
	Close(pMenus);
	m_SourcePath = pPath;
	m_SourceType = StorageType;
	m_Options = CQmScreenshotManager::CurrentWatermarkOptions();
	m_Options.m_Editor = true;
	m_Options.m_LogoLayer.m_Size = 15;
	m_Options.m_LogoLayer.m_X = 0;
	m_Options.m_TextLayer.m_X = m_Options.m_Position == CQmScreenshotManager::EWatermarkPosition::BOTTOM_RIGHT || m_Options.m_Position == CQmScreenshotManager::EWatermarkPosition::TOP_RIGHT ? 1000 : 0;
	m_Options.m_TextLayer.m_Y = m_Options.m_Position == CQmScreenshotManager::EWatermarkPosition::TOP_LEFT || m_Options.m_Position == CQmScreenshotManager::EWatermarkPosition::TOP_RIGHT ? 0 : 1000;
	m_TextInput.Set(m_Options.m_CustomText.c_str());
	m_ActiveLayer = 0;
	m_Open = m_Dirty = true;
	m_Failed = false;
	m_LastRequest = 0;
}

void CQmScreenshotEditor::Close(CMenus *pMenus)
{
	if(!m_Open && !m_Texture.IsValid() && !m_pPreview && !m_pExport)
		return;
	if(m_pPreview)
		m_pPreview->Cancel();
	if(m_pExport)
		m_pExport->Cancel();
	m_pPreview.reset();
	m_pExport.reset();
	m_pDialog.reset();
	if(m_Texture.IsValid())
		pMenus->Graphics()->UnloadTexture(&m_Texture);
	pMenus->Ui()->ReleaseActiveTextInput(&m_TextInput);
	if(pMenus->Ui()->ActiveItem() == &m_DragStart)
		pMenus->Ui()->SetActiveItem(nullptr);
	m_Width = m_Height = 0;
	m_aBounds = {};
	m_Open = false;
}

void CQmScreenshotEditor::Pump(CMenus *pMenus)
{
	if(m_pDialog)
	{
		bool Done;
		{
			const std::lock_guard<std::mutex> Lock(m_pDialog->m_Mutex);
			Done = m_pDialog->m_Done;
			if(Done && m_pDialog->m_Failed)
				qm_ui::Toast(EQmToastKind::FAILED, Localize("Unable to open the file picker"));
			if(Done && !m_pDialog->m_Path.empty())
			{
				m_Options.m_LogoPath = m_pDialog->m_Path;
				m_Dirty = true;
			}
		}
		if(Done)
			m_pDialog.reset();
	}
	if(m_pExport && m_pExport->Finished())
	{
		const bool Saved = m_pExport->State() == IJob::STATE_DONE && m_pExport->Saved();
		if(!m_pExport->Canceled())
			qm_ui::Toast(Saved ? EQmToastKind::SUCCESS : EQmToastKind::FAILED,
				Saved ? Localize("The watermarked screenshot was saved next to the original") : Localize("Unable to save the watermarked screenshot"));
		m_pExport.reset();
	}
	if(m_pPreview && m_pPreview->Finished())
	{
		// 同时只保留一个预览任务。连续拖动时也发布已完成帧，再请求最新参数。
		if(!m_pPreview->Canceled())
		{
			const auto *pImage = m_pPreview->State() == IJob::STATE_DONE ? m_pPreview->Preview() : nullptr;
			m_Failed = !pImage;
			if(pImage)
			{
				auto Texture = pMenus->Graphics()->LoadTextureRaw(*pImage, 0, "watermark-preview");
				if(Texture.IsValid())
				{
					if(m_Texture.IsValid())
						pMenus->Graphics()->UnloadTexture(&m_Texture);
					m_Texture = Texture;
					m_Width = pImage->m_Width;
					m_Height = pImage->m_Height;
					m_aBounds = m_pPreview->Bounds();
				}
				else
					m_Failed = true;
			}
		}
		m_pPreview.reset();
	}
	if(m_Dirty && !m_pPreview && time_get() - m_LastRequest >= time_freq() / 12)
	{
		m_Options.m_CustomText = m_aText;
		m_pPreview = pMenus->m_ScreenshotManager.CreateWatermarkJob(pMenus->Storage(), m_SourcePath.c_str(), m_SourceType, "screenshots/.watermark-preview.png", m_Options, 1200);
		m_Dirty = false;
		m_LastRequest = time_get();
		m_Failed = !m_pPreview;
		if(m_pPreview)
			pMenus->Engine()->AddJob(m_pPreview);
	}
}

void CQmScreenshotEditor::RenderImage(CMenus *pMenus, CUIRect Rect)
{
	Rect.Draw(ui_token::color::SCREENSHOT_EDITOR_BACKDROP, IGraphics::CORNER_ALL, 8);
	Rect.Margin(14, &Rect);
	if(!m_Texture.IsValid() || m_Width <= 0 || m_Height <= 0)
	{
		pMenus->Ui()->DoLabel(&Rect, m_Failed ? Localize("Could not preview this image") : Localize("Loading…"), 12, TEXTALIGN_MC);
		return;
	}
	const float Scale = std::min(Rect.w / m_Width, Rect.h / m_Height);
	CUIRect ImageRect{Rect.x + (Rect.w - m_Width * Scale) / 2, Rect.y + (Rect.h - m_Height * Scale) / 2, m_Width * Scale, m_Height * Scale};
	auto *pGraphics = pMenus->Graphics();
	// 棋盘批量绘制，只作为透明图片的预览背景，不写入导出像素。
	pGraphics->TextureClear();
	pGraphics->QuadsBegin();
	for(float Y = ImageRect.y; Y < ImageRect.y + ImageRect.h; Y += 12)
		for(float X = ImageRect.x; X < ImageRect.x + ImageRect.w; X += 12)
		{
			const float Shade = ((int)((X - ImageRect.x) / 12) + (int)((Y - ImageRect.y) / 12)) % 2 ? 0.20f : 0.26f;
			IGraphics::CQuadItem Cell(X, Y, std::min(12.0f, ImageRect.x + ImageRect.w - X), std::min(12.0f, ImageRect.y + ImageRect.h - Y));
			pGraphics->SetColor(Shade, Shade, Shade, 1);
			pGraphics->QuadsDrawTL(&Cell, 1);
		}
	pGraphics->QuadsEnd();
	pGraphics->TextureSet(m_Texture);
	pGraphics->WrapClamp();
	pGraphics->QuadsBegin();
	pGraphics->SetColor(1, 1, 1, 1);
	IGraphics::CQuadItem Quad(ImageRect.x, ImageRect.y, ImageRect.w, ImageRect.h);
	pGraphics->QuadsDrawTL(&Quad, 1);
	pGraphics->QuadsEnd();
	pGraphics->WrapNormal();

	const auto Bounds = m_aBounds[m_ActiveLayer];
	CUIRect Box{ImageRect.x + Bounds.m_X * Scale, ImageRect.y + Bounds.m_Y * Scale, Bounds.m_Width * Scale, Bounds.m_Height * Scale};
	auto &Layer = m_ActiveLayer == 0 ? m_Options.m_TextLayer : m_Options.m_LogoLayer;
	auto *pUi = pMenus->Ui();
	if(Box.w > 0 && Box.h > 0)
	{
		const ColorRGBA Outline(0.4f, 0.8f, 1, 0.9f);
		CUIRect Top{Box.x, Box.y, Box.w, 1}, Bottom{Box.x, Box.y + Box.h - 1, Box.w, 1}, Left{Box.x, Box.y, 1, Box.h}, Right{Box.x + Box.w - 1, Box.y, 1, Box.h};
		Top.Draw(Outline, 0, 0);
		Bottom.Draw(Outline, 0, 0);
		Left.Draw(Outline, 0, 0);
		Right.Draw(Outline, 0, 0);
		if(!pUi->ActiveItem() && pUi->MouseHovered(Layer.m_Tile ? &ImageRect : &Box) && pUi->MouseButtonClicked(0))
		{
			m_DragStart = vec2(pUi->MouseX(), pUi->MouseY());
			m_DragPosition = vec2(Layer.m_X, Layer.m_Y);
			const float Margin = std::min(ImageRect.w, ImageRect.h) * 0.02f;
			m_DragSpace = vec2(std::max(1.0f, ImageRect.w - Box.w - 2 * Margin), std::max(1.0f, ImageRect.h - Box.h - 2 * Margin));
			pUi->SetActiveItem(&m_DragStart);
		}
	}
	if(pUi->CheckActiveItem(&m_DragStart))
	{
		if(!pUi->MouseButton(0))
			pUi->SetActiveItem(nullptr);
		else
		{
			const int X = std::clamp((int)(m_DragPosition.x + (pUi->MouseX() - m_DragStart.x) / m_DragSpace.x * 1000), 0, 1000);
			const int Y = std::clamp((int)(m_DragPosition.y + (pUi->MouseY() - m_DragStart.y) / m_DragSpace.y * 1000), 0, 1000);
			m_Dirty |= X != Layer.m_X || Y != Layer.m_Y;
			Layer.m_X = X;
			Layer.m_Y = Y;
		}
	}
}

void CQmScreenshotEditor::Render(CMenus *pMenus, CUIRect Rect)
{
	Pump(pMenus);
	auto *pUi = pMenus->Ui();
	CUIRect Header, Back, Save, Canvas, Panel, Footer;
	Rect.HSplitTop(28, &Header, &Rect);
	Header.VSplitLeft(85, &Back, &Header);
	Header.VSplitRight(125, &Header, &Save);
	if(pMenus->DoButton_Menu(&m_aButtons[0], Localize("Back"), 0, &Back) || pUi->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		Close(pMenus);
		pMenus->DemolistPopulate();
		pMenus->DemolistOnUpdate(false);
		return;
	}
	pUi->DoLabel(&Header, Localize("Watermark editor"), 16, TEXTALIGN_MC);
	const bool CanSave = !m_Dirty && !m_pPreview && !m_Failed && m_Texture.IsValid();
	if(!m_pExport && !CanSave)
	{
		Save.Draw(ui_token::color::SCREENSHOT_EDITOR_DISABLED_ACTION, IGraphics::CORNER_ALL, 4);
		pUi->DoLabel(&Save, Localize("Save PNG copy"), 12, TEXTALIGN_MC);
	}
	else if(pMenus->DoButton_Menu(&m_aButtons[1], m_pExport ? Localize("Cancel") : Localize("Save PNG copy"), 0, &Save))
	{
		if(m_pExport)
			m_pExport->Cancel();
		else if(CanSave)
		{
			// 每次另存新名字；不替换原图或上一次导出。
			const auto Dot = m_SourcePath.find_last_of('.');
			m_TargetPath = m_SourcePath.substr(0, Dot) + "_watermarked_" + std::to_string(pid()) + "_" + std::to_string(time_get()) + ".png";
			m_Options.m_CustomText = m_aText;
			m_pExport = pMenus->m_ScreenshotManager.CreateWatermarkJob(pMenus->Storage(), m_SourcePath.c_str(), m_SourceType, m_TargetPath.c_str(), m_Options);
			if(m_pExport)
				pMenus->Engine()->AddJob(m_pExport);
			else
				qm_ui::Toast(EQmToastKind::FAILED, Localize("Unable to save the watermarked screenshot"));
		}
	}
	Rect.HSplitTop(8, nullptr, &Rect);
	Rect.HSplitBottom(22, &Rect, &Footer);
	const char *pStatus = Localize("Drag the selected watermark to move it. The original image is preserved.");
	if(m_Failed)
		pStatus = Localize("Preview failed. Check the image and Logo, then retry.");
	else if(m_Dirty || m_pPreview)
		pStatus = Localize("Updating preview…");
	else if(m_pExport)
		pStatus = Localize("Saving…");
	SLabelProperties StatusProps;
	StatusProps.m_MaxWidth = Footer.w;
	StatusProps.m_EllipsisAtEnd = true;
	StatusProps.m_EnableWidthCheck = false;
	pUi->DoLabel(&Footer, pStatus, 10, TEXTALIGN_ML, StatusProps);
	Rect.VSplitRight(std::min(240.0f, Rect.w * 0.42f), &Canvas, &Panel);
	Canvas.VSplitRight(8, &Canvas, nullptr);
	Panel.Draw(pMenus->MenuPanelColor(), IGraphics::CORNER_ALL, 8);
	Panel.Margin(8, &Panel);
	vec2 Offset;
	m_Scroll.Begin(&Panel, &Offset);
	Panel.y += Offset.y;
	CUIRect Content = Panel;
	Content.h = 435;
	m_Scroll.AddRect(Content);
	CUIRect Row;
	const auto Next = [&](float Height = 24.0f) {
		Content.HSplitTop(Height, &Row, &Content);
		Content.HSplitTop(5, nullptr, &Content);
	};
	Next();
	CUIRect TextTab, LogoTab;
	Row.VSplitMid(&TextTab, &LogoTab, 4);
	if(pMenus->DoButton_Menu(&m_aButtons[2], Localize("Text"), m_ActiveLayer == 0, &TextTab))
		m_ActiveLayer = 0;
	if(pMenus->DoButton_Menu(&m_aButtons[3], "Logo", m_ActiveLayer == 1, &LogoTab))
		m_ActiveLayer = 1;
	if(m_ActiveLayer == 0)
	{
		Next();
		m_Dirty |= pUi->DoEditBox(&m_TextInput, &Row, 12);
		Next();
		if(pMenus->DoButton_CheckBox(&m_aButtons[4], Localize("Timestamp"), m_Options.m_ShowTimestamp, &Row))
		{
			m_Options.m_ShowTimestamp = !m_Options.m_ShowTimestamp;
			m_Dirty = true;
		}
		Next();
		if(pMenus->DoButton_CheckBox(&m_aButtons[5], Localize("Map name"), m_Options.m_ShowMapName, &Row))
		{
			m_Options.m_ShowMapName = !m_Options.m_ShowMapName;
			m_Dirty = true;
		}
	}
	else
	{
		Next();
		if(pMenus->DoButton_Menu(&m_aButtons[6], Localize("Choose Logo (PNG / WebP)"), 0, &Row))
			ChooseLogo();
		Next();
		if(pMenus->DoButton_Menu(&m_aButtons[7], Localize("Remove Logo"), 0, &Row))
		{
			m_Options.m_LogoPath.clear();
			m_Dirty = true;
		}
		Next();
		SLabelProperties Props;
		Props.m_MaxWidth = Row.w;
		Props.m_EllipsisAtEnd = true;
		Props.m_EnableWidthCheck = false;
		pUi->DoLabel(&Row, m_Options.m_LogoPath.empty() ? Localize("No Logo selected") : m_Options.m_LogoPath.c_str(), 10, TEXTALIGN_ML, Props);
	}
	auto &Layer = m_ActiveLayer == 0 ? m_Options.m_TextLayer : m_Options.m_LogoLayer;
	Next();
	m_Dirty |= pUi->DoScrollbarOption(&Layer.m_Size, &Layer.m_Size, &Row, Localize("Size"), 3, 70, &CUi::ms_LinearScrollbarScale, 0, "%");
	Next();
	m_Dirty |= pUi->DoScrollbarOption(&Layer.m_Opacity, &Layer.m_Opacity, &Row, Localize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	Next();
	m_Dirty |= pUi->DoScrollbarOption(&Layer.m_Angle, &Layer.m_Angle, &Row, Localize("Rotation"), -180, 180);
	Next();
	if(pMenus->DoButton_CheckBox(&m_aButtons[8], Localize("Tile watermark"), Layer.m_Tile, &Row))
	{
		Layer.m_Tile = !Layer.m_Tile;
		m_Dirty = true;
	}
	Next(16);
	pUi->DoLabel(&Row, Localize("Alignment"), 11, TEXTALIGN_ML);
	for(int Y = 0; Y < 3; ++Y)
	{
		Next(22);
		for(int X = 0; X < 3; ++X)
		{
			CUIRect Button{Row.x + X * Row.w / 3, Row.y, Row.w / 3 - 3, Row.h};
			if(pMenus->DoButton_Menu(&m_aButtons[9 + Y * 3 + X], "•", Layer.m_X == X * 500 && Layer.m_Y == Y * 500, &Button))
			{
				Layer.m_X = X * 500;
				Layer.m_Y = Y * 500;
				m_Dirty = true;
			}
		}
	}
	Next();
	if(pMenus->DoButton_Menu(&m_aButtons[18], Localize("Reset layer"), 0, &Row))
	{
		Layer = {};
		Layer.m_Size = m_ActiveLayer ? 15 : 35;
		m_Dirty = true;
	}
	if(m_Failed)
	{
		Next();
		if(pMenus->DoButton_Menu(&m_aButtons[19], Localize("Retry"), 0, &Row))
			m_Dirty = true;
	}
	m_Scroll.End();
	RenderImage(pMenus, Canvas);
}
