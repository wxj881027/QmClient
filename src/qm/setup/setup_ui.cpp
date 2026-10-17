#include "setup_build.h"
#include "setup_engine.h"
#include "setup_motion.h"
#include "setup_options.h"

#include <commctrl.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
	constexpr float WIDTH = 744.0f;
	constexpr float HEIGHT = 434.0f;
	constexpr int ID_BROWSE = 100;
	constexpr int ID_DESKTOP = 101;
	constexpr int ID_LOG = 102;
	constexpr int ID_DIRECTORY = 103;

	D2D1_COLOR_F Color(UINT32 Rgb)
	{
		return D2D1::ColorF(Rgb);
	}

	UINT32 Blend(UINT32 From, UINT32 To, float Amount)
	{
		Amount = std::clamp(Amount, 0.0f, 1.0f);
		UINT32 Result = 0;
		for(const int Shift : {0, 8, 16})
		{
			const float Start = static_cast<float>((From >> Shift) & 255);
			const float End = static_cast<float>((To >> Shift) & 255);
			Result |= static_cast<UINT32>(std::lround(Start + (End - Start) * Amount)) << Shift;
		}
		return Result;
	}

	std::wstring RegistryString(HKEY Root, REGSAM View, const wchar_t *pName)
	{
		HKEY Key = nullptr;
		if(RegOpenKeyExW(Root, QM_SETUP_REGISTRY_KEY, 0, KEY_READ | View, &Key) != ERROR_SUCCESS)
			return {};
		DWORD Bytes = 0;
		std::wstring Result;
		if(RegGetValueW(Key, nullptr, pName, RRF_RT_REG_SZ, nullptr, nullptr, &Bytes) == ERROR_SUCCESS && Bytes <= 65536)
		{
			Result.resize(Bytes / sizeof(wchar_t));
			if(RegGetValueW(Key, nullptr, pName, RRF_RT_REG_SZ, nullptr, Result.data(), &Bytes) != ERROR_SUCCESS)
				Result.clear();
			else
				Result.resize(wcslen(Result.c_str()));
		}
		RegCloseKey(Key);
		return Result;
	}

	enum class EPage
	{
		OPTIONS,
		INSTALLING,
		DONE,
		FAILED
	};

	class CSetupWindow
	{
		QmSetup::SOptions m_Options;
		HWND m_Window = nullptr;
		HWND m_Edit = nullptr;
		HWND m_Browse = nullptr;
		HWND m_Desktop = nullptr;
		HWND m_Primary = nullptr;
		HWND m_Secondary = nullptr;
		HWND m_LogButton = nullptr;
		HWND m_Status = nullptr;
		HWND m_EngineWindow = nullptr;
		HWND m_HoverButton = nullptr;
		HFONT m_Font = nullptr;
		HBRUSH m_BackBrush = nullptr;
		UINT m_Dpi = 96;
		EPage m_Page = EPage::OPTIONS;
		bool m_Dark = false;
		bool m_Upgrade = false;
		bool m_CancelRequested = false;
		float m_Progress = 0;
		bool m_Animations = true;
		bool m_TimerRunning = false;
		std::chrono::steady_clock::time_point m_LastFrame;
		std::chrono::steady_clock::time_point m_InstallStart;
		QmSetup::CTransition m_Intro;
		QmSetup::CTransition m_Reveal{1.0f};
		QmSetup::CTransition m_VisualProgress;
		QmSetup::CTransition m_Check;
		struct SControlFeedback
		{
			HWND m_Window = nullptr;
			QmSetup::CTransition m_Hover;
			QmSetup::CTransition m_Press;
		};
		std::array<SControlFeedback, 5> m_aFeedback;
		DWORD m_ExitCode = 2;
		std::wstring m_Directory;
		std::wstring m_Error;
		std::unique_ptr<QmSetup::CSetupEngine> m_pEngine;
		ComPtr<ID2D1Factory> m_pFactory;
		ComPtr<IDWriteFactory> m_pWrite;
		ComPtr<ID2D1HwndRenderTarget> m_pTarget;
		ComPtr<ID2D1DCRenderTarget> m_pControlTarget;
		ComPtr<ID2D1Bitmap> m_pArtwork;
		ComPtr<ID2D1LinearGradientBrush> m_pHeroGradient;
		ComPtr<ID2D1StrokeStyle> m_pRoundStroke;
		struct STextFormat
		{
			float m_Size;
			bool m_Bold;
			bool m_Center;
			DWRITE_PARAGRAPH_ALIGNMENT m_Paragraph;
			ComPtr<IDWriteTextFormat> m_pFormat;
		};
		std::vector<STextFormat> m_vTextFormats;

		UINT32 Background() const { return m_Dark ? 0x221B22 : 0xFFF9FA; }
		UINT32 Foreground() const { return m_Dark ? 0xF8E8EE : 0x4A2433; }
		UINT32 Muted() const { return m_Dark ? 0xC4A9B5 : 0x866371; }
		UINT32 Border() const { return m_Dark ? 0x513642 : 0xE9D2DC; }
		UINT32 Accent(bool Pressed = false, bool Hover = false) const { return Pressed ? 0x94304F : Hover ? 0xC24D77 :
														    0xB23C62; }
		UINT32 FocusColor() const { return m_Dark ? 0xED9FB9 : 0xB23C62; }
		int Pixels(float Value) const { return static_cast<int>(std::lround(Value * m_Dpi / 96.0f)); }
		float ContentOffset() const { return 8.0f * (1.0f - m_Reveal.Value()); }
		UINT32 ContentColor(UINT32 Rgb) const { return Blend(Background(), Rgb, m_Reveal.Value()); }

		void UpdateAnimationTimer()
		{
			bool Active = m_Intro.Active() || m_Reveal.Active() || m_VisualProgress.Active() || m_Check.Active();
			for(const auto &Feedback : m_aFeedback)
				Active |= Feedback.m_Hover.Active() || Feedback.m_Press.Active();
			Active |= m_Animations && m_Page == EPage::INSTALLING && m_Progress == 0.0f;
			Active &= !IsIconic(m_Window);
			if(Active && !m_TimerRunning)
			{
				m_LastFrame = std::chrono::steady_clock::now();
				m_TimerRunning = SetTimer(m_Window, 2, 16, nullptr) != 0;
			}
			else if(!Active && m_TimerRunning)
			{
				KillTimer(m_Window, 2);
				m_TimerRunning = false;
			}
		}

		void UpdateControlFeedback(HWND Window, bool Hover, bool Pressed)
		{
			for(auto &Feedback : m_aFeedback)
			{
				if(Feedback.m_Window != Window)
					continue;
				Feedback.m_Hover.SetTarget(Hover ? 1.0f : 0.0f, m_Animations, 0.18f);
				Feedback.m_Press.SetTarget(Pressed ? 1.0f : 0.0f, m_Animations, 0.12f);
				InvalidateRect(Window, nullptr, FALSE);
			}
			UpdateAnimationTimer();
		}

		float ControlFeedback(HWND Window, bool Press) const
		{
			for(const auto &Feedback : m_aFeedback)
				if(Feedback.m_Window == Window)
					return Press ? Feedback.m_Press.Value() : Feedback.m_Hover.Value();
			return 0.0f;
		}

		void Fill(ID2D1RenderTarget *pTarget, D2D1_RECT_F Rect, UINT32 Rgb, float Radius = 0)
		{
			ComPtr<ID2D1SolidColorBrush> pBrush;
			if(SUCCEEDED(pTarget->CreateSolidColorBrush(Color(Rgb), &pBrush)))
				pTarget->FillRoundedRectangle(D2D1::RoundedRect(Rect, Radius, Radius), pBrush.Get());
		}

		void Stroke(ID2D1RenderTarget *pTarget, D2D1_RECT_F Rect, UINT32 Rgb, float Radius = 0)
		{
			ComPtr<ID2D1SolidColorBrush> pBrush;
			if(SUCCEEDED(pTarget->CreateSolidColorBrush(Color(Rgb), &pBrush)))
				pTarget->DrawRoundedRectangle(D2D1::RoundedRect(Rect, Radius, Radius), pBrush.Get());
		}

		void Text(ID2D1RenderTarget *pTarget, const std::wstring &Value, D2D1_RECT_F Rect, float Size, UINT32 Rgb, bool Bold = false, bool Center = false, DWRITE_PARAGRAPH_ALIGNMENT ParagraphAlignment = DWRITE_PARAGRAPH_ALIGNMENT_NEAR)
		{
			IDWriteTextFormat *pFormat = nullptr;
			for(const auto &Format : m_vTextFormats)
			{
				if(Format.m_Size == Size && Format.m_Bold == Bold && Format.m_Center == Center && Format.m_Paragraph == ParagraphAlignment)
				{
					pFormat = Format.m_pFormat.Get();
					break;
				}
			}
			if(!pFormat)
			{
				STextFormat Format{Size, Bold, Center, ParagraphAlignment, {}};
				if(FAILED(m_pWrite->CreateTextFormat(L"Microsoft YaHei UI", nullptr, Bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, Size, L"zh-CN", &Format.m_pFormat)))
					return;
				pFormat = Format.m_pFormat.Get();
				pFormat->SetTextAlignment(Center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
				pFormat->SetParagraphAlignment(Center ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : ParagraphAlignment);
				m_vTextFormats.push_back(std::move(Format));
			}
			ComPtr<ID2D1SolidColorBrush> pBrush;
			if(FAILED(pTarget->CreateSolidColorBrush(Color(Rgb), &pBrush)))
				return;
			pTarget->DrawText(Value.c_str(), static_cast<UINT32>(Value.size()), pFormat, Rect, pBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
		}

		void Checkmark(ID2D1RenderTarget *pTarget, D2D1_POINT_2F Center, float Size, UINT32 Rgb, float Progress)
		{
			ComPtr<ID2D1SolidColorBrush> pBrush;
			if(Progress <= 0.0f || FAILED(pTarget->CreateSolidColorBrush(Color(Rgb), &pBrush)))
				return;
			const D2D1_POINT_2F Start{Center.x - Size * 0.3f, Center.y};
			const D2D1_POINT_2F Bend{Center.x - Size * 0.1f, Center.y + Size * 0.2f};
			const D2D1_POINT_2F End{Center.x + Size * 0.3f, Center.y - Size * 0.2f};
			const float First = std::min(1.0f, Progress * 3.0f);
			pTarget->DrawLine(Start, {Start.x + (Bend.x - Start.x) * First, Start.y + (Bend.y - Start.y) * First}, pBrush.Get(), 2.0f, m_pRoundStroke.Get());
			if(Progress > 1.0f / 3.0f)
			{
				const float Second = std::min(1.0f, (Progress - 1.0f / 3.0f) * 1.5f);
				pTarget->DrawLine(Bend, {Bend.x + (End.x - Bend.x) * Second, Bend.y + (End.y - Bend.y) * Second}, pBrush.Get(), 2.0f, m_pRoundStroke.Get());
			}
		}

		void ReadTheme()
		{
			DWORD Light = 1;
			DWORD Bytes = sizeof(Light);
			RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &Light, &Bytes);
			m_Dark = Light == 0;
			m_pHeroGradient.Reset();
			if(m_BackBrush)
				DeleteObject(m_BackBrush);
			const auto Bg = Background();
			m_BackBrush = CreateSolidBrush(RGB((Bg >> 16) & 255, (Bg >> 8) & 255, Bg & 255));
			const BOOL Dark = m_Dark;
			DwmSetWindowAttribute(m_Window, 20, &Dark, sizeof(Dark));
			const UINT32 Caption = m_Dark ? 0x30212B : 0xFFE1EB;
			const UINT32 CaptionText = Foreground();
			const COLORREF CaptionColor = RGB((Caption >> 16) & 255, (Caption >> 8) & 255, Caption & 255);
			const COLORREF CaptionTextColor = RGB((CaptionText >> 16) & 255, (CaptionText >> 8) & 255, CaptionText & 255);
			// Windows 11 为原生标题栏配色；不支持的系统继续使用标准标题栏。
			DwmSetWindowAttribute(m_Window, DWMWA_CAPTION_COLOR, &CaptionColor, sizeof(CaptionColor));
			DwmSetWindowAttribute(m_Window, DWMWA_TEXT_COLOR, &CaptionTextColor, sizeof(CaptionTextColor));
			BOOL Animations = TRUE;
			SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &Animations, 0);
			m_Animations = Animations != FALSE;
			if(!m_Animations)
			{
				m_Intro.Finish();
				m_Reveal.Finish();
				m_VisualProgress.Finish();
				m_Check.Finish();
				for(auto &Feedback : m_aFeedback)
				{
					Feedback.m_Hover.Finish();
					Feedback.m_Press.Finish();
				}
				if(m_Edit)
					PlaceContent();
			}
			UpdateAnimationTimer();
			RedrawWindow(m_Window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
		}

		void PlaceContent()
		{
			const float Offset = ContentOffset();
			// 原生控件与自绘内容共用同一位移，绘制位置始终对应实际点击区域。
			struct SPlacement
			{
				HWND m_Control;
				float m_X, m_Y, m_W, m_H;
			};
			const SPlacement aControls[] = {
				{m_Edit, 310, 212, 278, 26},
				{m_Browse, 616, 198, 96, 48},
				{m_Desktop, 292, 266, 420, 36},
				{m_Primary, 512, 354, 200, 48},
				{m_Secondary, 296, 354, 200, 48},
				{m_LogButton, 296, 298, 150, 38},
				{m_Status, 296, 112, 416, m_Page == EPage::FAILED ? 168.0f : 48.0f}};
			HDWP Batch = BeginDeferWindowPos(7);
			for(const auto &Control : aControls)
			{
				if(!Batch)
					break;
				Batch = DeferWindowPos(Batch, Control.m_Control, nullptr, Pixels(Control.m_X), Pixels(Control.m_Y + Offset), Pixels(Control.m_W), Pixels(Control.m_H), SWP_NOZORDER | SWP_NOACTIVATE);
			}
			if(!Batch || !EndDeferWindowPos(Batch))
				for(const auto &Control : aControls)
					SetWindowPos(Control.m_Control, nullptr, Pixels(Control.m_X), Pixels(Control.m_Y + Offset), Pixels(Control.m_W), Pixels(Control.m_H), SWP_NOZORDER | SWP_NOACTIVATE);
		}

		void Layout()
		{
			const HFONT OldFont = m_Font;
			m_Font = CreateFontW(-Pixels(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
			for(const HWND Control : {m_Edit, m_Browse, m_Desktop, m_Primary, m_Secondary, m_LogButton, m_Status})
				SendMessageW(Control, WM_SETFONT, reinterpret_cast<WPARAM>(m_Font), TRUE);
			if(OldFont)
				DeleteObject(OldFont);
			PlaceContent();
		}

		void ShowPage(EPage Page)
		{
			m_Page = Page;
			m_Reveal.Snap(0.0f);
			m_Reveal.SetTarget(1.0f, m_Animations, 0.28f);
			m_HoverButton = nullptr;
			for(auto &Feedback : m_aFeedback)
			{
				Feedback.m_Hover.Snap(0.0f);
				Feedback.m_Press.Snap(0.0f);
			}
			const bool Options = Page == EPage::OPTIONS;
			for(const HWND Control : {m_Edit, m_Browse, m_Desktop})
				ShowWindow(Control, Options ? SW_SHOW : SW_HIDE);
			ShowWindow(m_Primary, Page == EPage::INSTALLING ? SW_HIDE : SW_SHOW);
			ShowWindow(m_LogButton, Page == EPage::FAILED && m_pEngine && !m_pEngine->Log().empty() ? SW_SHOW : SW_HIDE);
			SetWindowTextW(m_Primary, Page == EPage::DONE ? L"立即启动" : Page == EPage::FAILED ? L"返回安装选项" :
									      m_Upgrade                     ? L"更新安装" :
													      L"开始安装");
			SetWindowTextW(m_Secondary, Page == EPage::DONE ? L"完成" : Page == EPage::FAILED ? L"关闭" :
													    L"取消");
			const std::wstring Status = Page == EPage::OPTIONS ? (m_Upgrade ? L"已找到旧版本，确认位置即可更新。" : L"选择安装位置，然后开始安装。") : Page == EPage::INSTALLING ? L"请稍候，完成后即可开始。" :
																			   Page == EPage::DONE               ? L"QmClient 已安装完成，可以立即启动。" :
																							       m_Error;
			SetWindowTextW(m_Status, Status.c_str());
			PlaceContent();
			EnableWindow(m_Secondary, TRUE);
			SetFocus(Page == EPage::INSTALLING ? m_Secondary : m_Primary);
			UpdateAnimationTimer();
			RedrawWindow(m_Window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
		}

		void Browse()
		{
			ComPtr<IFileOpenDialog> pDialog;
			if(FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDialog))))
				return;
			DWORD Flags = 0;
			pDialog->GetOptions(&Flags);
			pDialog->SetOptions(Flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
			pDialog->SetTitle(L"选择 QmClient 安装文件夹");
			if(SUCCEEDED(pDialog->Show(m_Window)))
			{
				ComPtr<IShellItem> pItem;
				PWSTR pPath = nullptr;
				if(SUCCEEDED(pDialog->GetResult(&pItem)) && SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pPath)))
				{
					SetWindowTextW(m_Edit, pPath);
					CoTaskMemFree(pPath);
				}
			}
		}

		void BeginInstall()
		{
			std::wstring Directory(static_cast<size_t>(GetWindowTextLengthW(m_Edit)) + 1, L'\0');
			GetWindowTextW(m_Edit, Directory.data(), static_cast<int>(Directory.size()));
			Directory.resize(wcslen(Directory.c_str()));
			if(Directory.empty())
			{
				MessageBoxW(m_Window, L"请输入安装位置。", L"QmClient", MB_OK | MB_ICONINFORMATION);
				SetFocus(m_Edit);
				return;
			}
			m_Directory = Directory;
			m_EngineWindow = nullptr;
			m_CancelRequested = false;
			m_Progress = 0;
			m_VisualProgress.Snap(0.0f);
			m_InstallStart = std::chrono::steady_clock::now();
			m_pEngine = std::make_unique<QmSetup::CSetupEngine>();
			ShowPage(EPage::INSTALLING);
			try
			{
				m_pEngine->Start(QmSetup::NativeArguments(m_Options, Directory, SendMessageW(m_Desktop, BM_GETCHECK, 0, 0) == BST_CHECKED), m_Window);
			}
			catch(const std::exception &)
			{
				m_Error = L"无法创建安装任务，请重新运行安装程序。";
				m_ExitCode = 1;
				ShowPage(EPage::FAILED);
			}
		}

		void Close()
		{
			if(m_Page != EPage::INSTALLING)
			{
				DestroyWindow(m_Window);
				return;
			}
			if(!m_CancelRequested && MessageBoxW(m_Window, L"要取消本次安装吗？安装程序会完成必要的清理后退出安装。", L"取消安装", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES)
			{
				m_CancelRequested = true;
				SetWindowTextW(m_Status, L"正在取消，请稍候。");
				SetWindowTextW(m_Secondary, L"正在取消");
				EnableWindow(m_Secondary, FALSE);
				InvalidateRect(m_Window, nullptr, FALSE);
				if(m_EngineWindow)
					PostMessageW(m_EngineWindow, WM_CLOSE, 0, 0);
			}
		}

		void Activate(int Id)
		{
			if(Id == IDCANCEL)
				Close();
			else if(Id == ID_BROWSE && m_Page == EPage::OPTIONS)
				Browse();
			else if(Id == ID_LOG && m_Page == EPage::FAILED && m_pEngine)
				ShellExecuteW(m_Window, L"open", m_pEngine->Log().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			else if(Id == IDOK)
			{
				if(m_Page == EPage::OPTIONS)
					BeginInstall();
				else if(m_Page == EPage::FAILED)
					ShowPage(EPage::OPTIONS);
				else if(m_Page == EPage::DONE)
				{
					const auto Executable = m_Directory + L"\\DDNet.exe";
					if(reinterpret_cast<INT_PTR>(ShellExecuteW(m_Window, L"open", Executable.c_str(), nullptr, m_Directory.c_str(), SW_SHOWNORMAL)) > 32)
						DestroyWindow(m_Window);
					else
						MessageBoxW(m_Window, L"客户端未能启动，可以从开始菜单打开 QmClient。", L"QmClient", MB_OK | MB_ICONERROR);
				}
			}
		}

		bool EnsureTarget()
		{
			if(m_pTarget)
				return true;
			RECT Rect{};
			GetClientRect(m_Window, &Rect);
			if(FAILED(m_pFactory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(m_Window, D2D1::SizeU(Rect.right, Rect.bottom)), &m_pTarget)))
				return false;
			m_pTarget->SetDpi(static_cast<float>(m_Dpi), static_cast<float>(m_Dpi));
			const HMODULE Module = GetModuleHandleW(nullptr);
			const HRSRC Resource = FindResourceW(Module, MAKEINTRESOURCEW(102), RT_RCDATA);
			const HGLOBAL Loaded = Resource ? LoadResource(Module, Resource) : nullptr;
			auto *pData = Loaded ? static_cast<BYTE *>(LockResource(Loaded)) : nullptr;
			const DWORD DataSize = Resource ? SizeofResource(Module, Resource) : 0;
			ComPtr<IWICImagingFactory> pWic;
			ComPtr<IWICStream> pStream;
			ComPtr<IWICBitmapDecoder> pDecoder;
			ComPtr<IWICBitmapFrameDecode> pFrame;
			ComPtr<IWICFormatConverter> pConverter;
			if(pData && DataSize > 0 && SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pWic))) &&
				SUCCEEDED(pWic->CreateStream(&pStream)) &&
				SUCCEEDED(pStream->InitializeFromMemory(pData, DataSize)) &&
				SUCCEEDED(pWic->CreateDecoderFromStream(pStream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &pDecoder)) &&
				SUCCEEDED(pDecoder->GetFrame(0, &pFrame)) &&
				SUCCEEDED(pWic->CreateFormatConverter(&pConverter)) &&
				SUCCEEDED(pConverter->Initialize(pFrame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
				m_pTarget->CreateBitmapFromWicBitmap(pConverter.Get(), nullptr, &m_pArtwork);
			return true;
		}

		void Paint()
		{
			PAINTSTRUCT PaintInfo{};
			BeginPaint(m_Window, &PaintInfo);
			if(EnsureTarget())
			{
				auto *pTarget = m_pTarget.Get();
				pTarget->BeginDraw();
				pTarget->Clear(Color(Background()));
				if(!m_pHeroGradient)
				{
					const D2D1_GRADIENT_STOP aStops[] = {{0.0f, Color(m_Dark ? 0x503140 : 0xFBE0E9)}, {1.0f, Color(m_Dark ? 0x30212B : 0xF8EDF1)}};
					ComPtr<ID2D1GradientStopCollection> pStops;
					if(SUCCEEDED(pTarget->CreateGradientStopCollection(aStops, 2, &pStops)))
						pTarget->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({16, 16}, {264, 418}), pStops.Get(), &m_pHeroGradient);
				}
				if(m_pHeroGradient)
					pTarget->FillRoundedRectangle(D2D1::RoundedRect({16, 16, 264, 418}, 24, 24), m_pHeroGradient.Get());
				else
					Fill(pTarget, {16, 16, 264, 418}, m_Dark ? 0x382530 : 0xFBE0E9, 24);
				Stroke(pTarget, {16.5f, 16.5f, 263.5f, 417.5f}, m_Dark ? 0x583848 : 0xF2D3DF, 24);
				const float Intro = std::clamp(m_Intro.Value(), 0.0f, 1.0f);
				const float IntroOffset = 10.0f * (1.0f - Intro);
				if(m_pArtwork)
				{
					const auto Size = m_pArtwork->GetSize();
					const float Scale = std::min(216.0f / Size.width, 274.0f / Size.height);
					const float ArtWidth = Size.width * Scale;
					const float ArtHeight = Size.height * Scale;
					const float Left = 140.0f - ArtWidth * 0.5f;
					const float Top = 185.0f - ArtHeight * 0.5f + IntroOffset;
					pTarget->DrawBitmap(m_pArtwork.Get(), {Left, Top, Left + ArtWidth, Top + ArtHeight}, Intro, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
				}
				Text(pTarget, L"QmClient", {36, 340, 244, 377}, 24, Foreground(), true);
				Text(pTarget, L"VERSION " QM_SETUP_VERSION, {36, 380, 244, 402}, 11, Muted());
				// 左侧品牌保持稳定；右侧内容与原生控件沿同一轨道进入。
				pTarget->SetTransform(D2D1::Matrix3x2F::Translation(0, ContentOffset()));
				const UINT32 Fg = ContentColor(Foreground());
				const UINT32 Secondary = ContentColor(Muted());
				const UINT32 Line = ContentColor(Border());
				const UINT32 Rose = ContentColor(Accent());
				if(m_Page == EPage::OPTIONS)
				{
					Text(pTarget, m_Upgrade ? L"更新 QmClient" : L"安装 QmClient", {296, 52, 712, 96}, 28, Fg, true);
					Text(pTarget, L"安装位置", {296, 174, 712, 195}, 12, Secondary);
					Stroke(pTarget, {296, 198, 604, 246}, GetFocus() == m_Edit ? ContentColor(FocusColor()) : Line, 12);
				}
				else if(m_Page == EPage::INSTALLING)
				{
					Text(pTarget, m_CancelRequested ? L"正在取消" : L"正在安装", {296, 52, 712, 96}, 28, Fg, true);
					Text(pTarget, m_Progress > 0 ? std::to_wstring(std::min(99, static_cast<int>(m_Progress * 100))) + L"%" : L"准备中", {296, 194, 712, 260}, m_Progress > 0 ? 44.0f : 32.0f, Fg, true);
					Fill(pTarget, {296, 282, 712, 290}, Line, 4);
					if(m_Progress > 0)
					{
						const float ProgressWidth = 416 * std::clamp(m_VisualProgress.Value(), 0.0f, 1.0f);
						if(ProgressWidth > 0)
							Fill(pTarget, {296, 282, 296 + ProgressWidth, 290}, Rose, std::min(4.0f, ProgressWidth * 0.5f));
					}
					else
					{
						// 往返速度在端点降为零，未知进度不会瞬移回起点。
						const float Phase = std::fmod(std::chrono::duration<float>(std::chrono::steady_clock::now() - m_InstallStart).count(), 2.4f) / 2.4f;
						const float Offset = m_Animations ? (1.0f - std::cos(Phase * 6.2831853f)) * 160.0f : 0.0f;
						Fill(pTarget, {296 + Offset, 282, 392 + Offset, 290}, Rose, 4);
					}
				}
				else if(m_Page == EPage::DONE)
				{
					Text(pTarget, L"安装完成", {296, 52, 712, 96}, 28, Fg, true);
					Fill(pTarget, {296, 188, 712, 302}, ContentColor(m_Dark ? 0x35232E : 0xFAEBF0), 16);
					Fill(pTarget, {316, 208, 348, 240}, Rose, 16);
					Checkmark(pTarget, {332, 224}, 24, ContentColor(0xFFFFFF), m_Reveal.Value());
					Text(pTarget, L"安装位置", {362, 212, 692, 239}, 12, Secondary);
					Text(pTarget, m_Directory, {316, 252, 692, 290}, 12, Fg);
				}
				else
				{
					Text(pTarget, QmSetup::IsCancelled(m_ExitCode, m_CancelRequested) ? L"安装已取消" : L"安装未完成", {296, 52, 712, 96}, 28, Fg, true);
				}
				pTarget->SetTransform(D2D1::Matrix3x2F::Identity());
				if(pTarget->EndDraw() == D2DERR_RECREATE_TARGET)
				{
					m_pArtwork.Reset();
					m_pHeroGradient.Reset();
					m_pTarget.Reset();
				}
			}
			EndPaint(m_Window, &PaintInfo);
		}

		ID2D1DCRenderTarget *ControlTarget(HDC Dc, const RECT &Rect)
		{
			if(!m_pControlTarget)
			{
				auto Properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
				if(FAILED(m_pFactory->CreateDCRenderTarget(&Properties, &m_pControlTarget)))
					return nullptr;
			}
			if(FAILED(m_pControlTarget->BindDC(Dc, &Rect)))
				return nullptr;
			auto *pTarget = m_pControlTarget.Get();
			pTarget->SetDpi(static_cast<float>(m_Dpi), static_cast<float>(m_Dpi));
			return pTarget;
		}

		bool DrawCheckbox(const NMCUSTOMDRAW &Item)
		{
			auto *pTarget = ControlTarget(Item.hdc, Item.rc);
			if(!pTarget)
				return false;
			const HWND Control = Item.hdr.hwndFrom;
			const float Width = (Item.rc.right - Item.rc.left) * 96.0f / m_Dpi;
			const float Height = (Item.rc.bottom - Item.rc.top) * 96.0f / m_Dpi;
			const float CenterY = Height * 0.5f;
			const auto State = SendMessageW(Control, BM_GETSTATE, 0, 0);
			const bool Disabled = !IsWindowEnabled(Control);
			const float Press = Disabled ? 0.0f : std::clamp(ControlFeedback(Control, true), 0.0f, 1.0f);
			const float Hover = Disabled ? 0.0f : std::clamp(ControlFeedback(Control, false), 0.0f, 1.0f);
			const float Check = std::clamp(m_Check.Value(), 0.0f, 1.0f);
			const bool Focus = (State & BST_FOCUS) != 0 && (SendMessageW(Control, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS) == 0;
			const UINT32 CheckColor = Blend(Blend(Accent(), Accent(false, true), Hover), Accent(true), Press);
			wchar_t aCaption[128]{};
			GetWindowTextW(Control, aCaption, 128);
			pTarget->BeginDraw();
			pTarget->Clear(Color(Background()));
			Fill(pTarget, {1, 1, Width - 1, Height - 1}, ContentColor(Blend(Background(), m_Dark ? 0x382530 : 0xFCEBF1, std::max(Hover, Press))), 8);
			const D2D1_RECT_F Box{4 + Press, CenterY - 10 + Press, 24 - Press, CenterY + 10 - Press};
			Fill(pTarget, Box, ContentColor(Disabled ? Border() : Blend(m_Dark ? 0x2C2029 : 0xFFFFFF, CheckColor, Check)), 5);
			Stroke(pTarget, Box, ContentColor(Disabled ? Border() : Blend(m_Dark ? 0xA17A8C : 0xB18A9A, CheckColor, std::max(Check, Hover))), 5);
			Checkmark(pTarget, {14, CenterY}, 20 - Press * 2, ContentColor(Disabled ? Muted() : 0xFFFFFF), Check);
			Text(pTarget, aCaption, {36, 0, Width - 4, Height}, 14, ContentColor(Disabled ? Muted() : Foreground()), false, false, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
			if(Focus)
				Stroke(pTarget, {1, 1, Width - 1, Height - 1}, ContentColor(FocusColor()), 8);
			const HRESULT Result = pTarget->EndDraw();
			if(Result == D2DERR_RECREATE_TARGET)
				m_pControlTarget.Reset();
			return SUCCEEDED(Result);
		}

		void DrawButton(const DRAWITEMSTRUCT &Item)
		{
			auto *pTarget = ControlTarget(Item.hDC, Item.rcItem);
			if(!pTarget)
				return;
			const float Width = (Item.rcItem.right - Item.rcItem.left) * 96.0f / m_Dpi;
			const float Height = (Item.rcItem.bottom - Item.rcItem.top) * 96.0f / m_Dpi;
			const bool Primary = Item.CtlID == IDOK;
			const bool Disabled = (Item.itemState & ODS_DISABLED) != 0;
			const float Press = Disabled ? 0.0f : std::clamp(ControlFeedback(Item.hwndItem, true), 0.0f, 1.0f);
			const float Hover = Disabled ? 0.0f : std::clamp(ControlFeedback(Item.hwndItem, false), 0.0f, 1.0f);
			wchar_t aCaption[128]{};
			GetWindowTextW(Item.hwndItem, aCaption, 128);
			pTarget->BeginDraw();
			pTarget->Clear(Color(Background()));
			const UINT32 ButtonColor = Primary ? Blend(Blend(Accent(), Accent(false, true), Hover), Accent(true), Press) : Blend(Blend(Background(), m_Dark ? 0x382530 : 0xFCEBF1, Hover), Border(), Press);
			const D2D1_RECT_F Face{1 + Press, 1 + Press, Width - 1 - Press, Height - 1 - Press};
			Fill(pTarget, Face, ContentColor(Disabled ? Border() : ButtonColor), 12);
			if(!Primary)
				Stroke(pTarget, Face, ContentColor(Border()), 12);
			Text(pTarget, aCaption, {5, 1 + Press, Width - 5, Height - 1 + Press}, 14, ContentColor(Disabled ? Muted() : Primary ? 0xFFFFFF :
																	       Foreground()),
				Primary, true);
			if((Item.itemState & ODS_FOCUS) && !(Item.itemState & ODS_NOFOCUSRECT))
				Stroke(pTarget, {4 + Press, 4 + Press, Width - 4 - Press, Height - 4 - Press}, ContentColor(Primary ? 0xFFE0EB : FocusColor()), 9);
			if(pTarget->EndDraw() == D2DERR_RECREATE_TARGET)
				m_pControlTarget.Reset();
		}

		LRESULT Message(UINT Message, WPARAM WParam, LPARAM LParam)
		{
			switch(Message)
			{
			case WM_PAINT: Paint(); return 0;
			case WM_ERASEBKGND: return 1;
			case WM_COMMAND:
				if(LOWORD(WParam) == ID_DIRECTORY && (HIWORD(WParam) == EN_SETFOCUS || HIWORD(WParam) == EN_KILLFOCUS))
					InvalidateRect(m_Window, nullptr, FALSE);
				else if(LOWORD(WParam) == ID_DESKTOP && HIWORD(WParam) == BN_CLICKED)
				{
					m_Check.SetTarget(SendMessageW(m_Desktop, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1.0f : 0.0f, m_Animations, 0.18f);
					InvalidateRect(m_Desktop, nullptr, FALSE);
					UpdateAnimationTimer();
				}
				else if(HIWORD(WParam) == BN_CLICKED)
					Activate(LOWORD(WParam));
				return 0;
			case WM_DRAWITEM: DrawButton(*reinterpret_cast<DRAWITEMSTRUCT *>(LParam)); return TRUE;
			case WM_NOTIFY:
			{
				const auto *pHeader = reinterpret_cast<const NMHDR *>(LParam);
				if(pHeader->hwndFrom == m_Desktop && pHeader->code == NM_CUSTOMDRAW)
				{
					const auto &Item = *reinterpret_cast<const NMCUSTOMDRAW *>(LParam);
					if(Item.dwDrawStage == CDDS_PREPAINT && DrawCheckbox(Item))
						return CDRF_SKIPDEFAULT;
					return CDRF_DODEFAULT;
				}
				return DefWindowProcW(m_Window, Message, WParam, LParam);
			}
			case WM_CTLCOLOREDIT:
			case WM_CTLCOLORSTATIC:
			case WM_CTLCOLORBTN:
			{
				const auto Fg = ContentColor(reinterpret_cast<HWND>(LParam) == m_Status && m_Page != EPage::FAILED ? Muted() : Foreground());
				const auto Bg = Background();
				SetTextColor(reinterpret_cast<HDC>(WParam), RGB((Fg >> 16) & 255, (Fg >> 8) & 255, Fg & 255));
				SetBkColor(reinterpret_cast<HDC>(WParam), RGB((Bg >> 16) & 255, (Bg >> 8) & 255, Bg & 255));
				return reinterpret_cast<LRESULT>(m_BackBrush);
			}
			case WM_SETTINGCHANGE: ReadTheme(); return 0;
			case WM_DPICHANGED:
			{
				m_Dpi = HIWORD(WParam);
				const auto *pRect = reinterpret_cast<RECT *>(LParam);
				if(m_pTarget)
					m_pTarget->SetDpi(static_cast<float>(m_Dpi), static_cast<float>(m_Dpi));
				SetWindowPos(m_Window, nullptr, pRect->left, pRect->top, pRect->right - pRect->left, pRect->bottom - pRect->top, SWP_NOZORDER | SWP_NOACTIVATE);
				Layout();
				return 0;
			}
			case WM_SIZE:
				if(m_pTarget && WParam != SIZE_MINIMIZED)
					m_pTarget->Resize(D2D1::SizeU(LOWORD(LParam), HIWORD(LParam)));
				UpdateAnimationTimer();
				return 0;
			case WM_TIMER:
			{
				if(WParam != 2)
					return 0;
				const auto Now = std::chrono::steady_clock::now();
				const float Seconds = std::chrono::duration<float>(Now - m_LastFrame).count();
				m_LastFrame = Now;
				const bool Reveal = m_Reveal.Active();
				const bool Check = m_Check.Active();
				const bool Main = Reveal || m_Intro.Active() || m_VisualProgress.Active() || (m_Page == EPage::INSTALLING && m_Progress == 0.0f);
				m_Intro.Advance(Seconds);
				m_Reveal.Advance(Seconds);
				m_VisualProgress.Advance(Seconds);
				m_Check.Advance(Seconds);
				for(auto &Feedback : m_aFeedback)
				{
					const bool Changed = Feedback.m_Hover.Active() || Feedback.m_Press.Active();
					Feedback.m_Hover.Advance(Seconds);
					Feedback.m_Press.Advance(Seconds);
					if(Changed)
						InvalidateRect(Feedback.m_Window, nullptr, FALSE);
				}
				if(Reveal)
				{
					PlaceContent();
					RedrawWindow(m_Window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
				}
				else if(Main)
					InvalidateRect(m_Window, nullptr, FALSE);
				if(Check)
					InvalidateRect(m_Desktop, nullptr, FALSE);
				UpdateAnimationTimer();
				return 0;
			}
			case QmSetup::WM_ENGINE_READY:
				if(m_Page == EPage::INSTALLING)
				{
					m_EngineWindow = reinterpret_cast<HWND>(WParam);
					if(m_CancelRequested)
						PostMessageW(m_EngineWindow, WM_CLOSE, 0, 0);
				}
				return 0;
			case QmSetup::WM_ENGINE_PROGRESS:
				if(m_Page == EPage::INSTALLING && LParam > 0)
				{
					m_Progress = std::clamp(static_cast<float>(WParam) / static_cast<float>(LParam), 0.0f, 0.99f);
					m_VisualProgress.SetTarget(m_Progress, m_Animations, 0.24f);
					UpdateAnimationTimer();
					InvalidateRect(m_Window, nullptr, FALSE);
				}
				return 0;
			case QmSetup::WM_ENGINE_DONE:
				if(m_Page == EPage::INSTALLING && m_pEngine)
				{
					m_pEngine->Join();
					m_EngineWindow = nullptr;
					m_ExitCode = static_cast<DWORD>(WParam);
					m_Error = m_pEngine->Error();
					if(QmSetup::IsCancelled(m_ExitCode, m_CancelRequested))
						m_Error = L"安装已取消。可以返回安装选项后重试。";
					ShowPage(m_ExitCode == 0 ? EPage::DONE : EPage::FAILED);
				}
				return 0;
			case WM_CLOSE: Close(); return 0;
			case WM_QUERYENDSESSION: return m_Page != EPage::INSTALLING;
			case WM_DESTROY:
				KillTimer(m_Window, 2);
				m_TimerRunning = false;
				PostQuitMessage(static_cast<int>(m_ExitCode));
				return 0;
			default: return DefWindowProcW(m_Window, Message, WParam, LParam);
			}
		}

		static LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
		{
			auto *pSelf = reinterpret_cast<CSetupWindow *>(GetWindowLongPtrW(Window, GWLP_USERDATA));
			if(Message == WM_NCCREATE)
			{
				pSelf = static_cast<CSetupWindow *>(reinterpret_cast<CREATESTRUCTW *>(LParam)->lpCreateParams);
				pSelf->m_Window = Window;
				SetWindowLongPtrW(Window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pSelf));
			}
			return pSelf ? pSelf->Message(Message, WParam, LParam) : DefWindowProcW(Window, Message, WParam, LParam);
		}

		static LRESULT CALLBACK ButtonProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam, UINT_PTR, DWORD_PTR Data)
		{
			auto *pSelf = reinterpret_cast<CSetupWindow *>(Data);
			if(Message == WM_NCDESTROY)
			{
				if(pSelf->m_HoverButton == Window)
					pSelf->m_HoverButton = nullptr;
				RemoveWindowSubclass(Window, ButtonProc, 1);
				return DefSubclassProc(Window, Message, WParam, LParam);
			}
			if(Message == WM_MOUSEMOVE && pSelf->m_HoverButton != Window)
			{
				if(pSelf->m_HoverButton)
					pSelf->UpdateControlFeedback(pSelf->m_HoverButton, false, false);
				pSelf->m_HoverButton = Window;
				TRACKMOUSEEVENT Track{sizeof(Track), TME_LEAVE, Window, 0};
				TrackMouseEvent(&Track);
			}
			else if(Message == WM_MOUSELEAVE)
			{
				if(pSelf->m_HoverButton == Window)
					pSelf->m_HoverButton = nullptr;
			}
			const LRESULT Result = DefSubclassProc(Window, Message, WParam, LParam);
			if(!IsWindow(Window))
				return Result;
			switch(Message)
			{
			case WM_MOUSEMOVE:
			case WM_MOUSELEAVE:
			case WM_LBUTTONDOWN:
			case WM_LBUTTONUP:
			case WM_KEYDOWN:
			case WM_KEYUP:
			case WM_CAPTURECHANGED:
			case WM_ENABLE:
			case BM_SETSTATE:
				pSelf->UpdateControlFeedback(Window, IsWindowEnabled(Window) && pSelf->m_HoverButton == Window, IsWindowEnabled(Window) && (SendMessageW(Window, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0);
				break;
			case BM_SETCHECK:
				if(Window == pSelf->m_Desktop)
				{
					pSelf->m_Check.SetTarget(SendMessageW(Window, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1.0f : 0.0f, pSelf->m_Animations, 0.18f);
					pSelf->UpdateAnimationTimer();
					InvalidateRect(Window, nullptr, FALSE);
				}
				break;
			}
			return Result;
		}

	public:
		explicit CSetupWindow(QmSetup::SOptions Options) :
			m_Options(std::move(Options)) {}
		~CSetupWindow()
		{
			if(m_Font)
				DeleteObject(m_Font);
			if(m_BackBrush)
				DeleteObject(m_BackBrush);
		}

		int Run(HINSTANCE Instance, int Show)
		{
			if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_pFactory.GetAddressOf())) || FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown **>(m_pWrite.GetAddressOf()))))
			{
				MessageBoxW(nullptr, L"无法初始化安装界面。", L"QmClient", MB_OK | MB_ICONERROR);
				return 1;
			}
			m_pFactory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND), nullptr, 0, &m_pRoundStroke);
			bool DesktopIcon = false;
			// 当前内核使用 PrivilegesRequired=lowest，只读取当前用户的安装记录。
			for(const REGSAM View : {KEY_WOW64_64KEY, KEY_WOW64_32KEY})
			{
				m_Directory = RegistryString(HKEY_CURRENT_USER, View, L"Inno Setup: App Path");
				if(!m_Directory.empty())
				{
					const auto Tasks = QmSetup::Lower(RegistryString(HKEY_CURRENT_USER, View, L"Inno Setup: Selected Tasks"));
					DesktopIcon = Tasks.find(L"desktopicon") != std::wstring::npos;
					break;
				}
			}
			m_Upgrade = !m_Directory.empty();
			if(!m_Options.m_Directory.empty())
				m_Directory = m_Options.m_Directory;
			if(m_Directory.empty())
			{
				PWSTR pLocal = nullptr;
				if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &pLocal)))
					return 1;
				m_Directory = std::wstring(pLocal) + L"\\Programs\\QmClient";
				CoTaskMemFree(pLocal);
			}
			// 新版 DPI API 动态读取，旧系统仍可使用现有安装内核支持的路径。
			const HMODULE User32 = GetModuleHandleW(L"user32.dll");
			using FGetDpi = UINT(WINAPI *)();
			const auto pGetDpi = reinterpret_cast<FGetDpi>(GetProcAddress(User32, "GetDpiForSystem"));
			if(pGetDpi)
				m_Dpi = pGetDpi();
			else
			{
				const HDC Screen = GetDC(nullptr);
				m_Dpi = Screen ? static_cast<UINT>(GetDeviceCaps(Screen, LOGPIXELSX)) : 96;
				if(Screen)
					ReleaseDC(nullptr, Screen);
			}
			WNDCLASSEXW Class{};
			Class.cbSize = sizeof(Class);
			Class.hInstance = Instance;
			Class.lpfnWndProc = WindowProc;
			Class.lpszClassName = L"QmClientSetupWindow";
			Class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			Class.hIcon = LoadIconW(Instance, MAKEINTRESOURCEW(1));
			if(!RegisterClassExW(&Class))
				return 1;
			const DWORD Style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
			RECT Rect{0, 0, Pixels(WIDTH), Pixels(HEIGHT)};
			using FAdjustRect = BOOL(WINAPI *)(LPRECT, DWORD, BOOL, DWORD, UINT);
			const auto pAdjustRect = reinterpret_cast<FAdjustRect>(GetProcAddress(User32, "AdjustWindowRectExForDpi"));
			if(pAdjustRect)
				pAdjustRect(&Rect, Style, FALSE, 0, m_Dpi);
			else
				AdjustWindowRectEx(&Rect, Style, FALSE, 0);
			const std::wstring Title = L"QmClient " QM_SETUP_VERSION L" 安装程序";
			if(!CreateWindowExW(0, Class.lpszClassName, Title.c_str(), Style, CW_USEDEFAULT, CW_USEDEFAULT, Rect.right - Rect.left, Rect.bottom - Rect.top, nullptr, nullptr, Instance, this))
				return 1;
			ReadTheme();
			const DWORD ControlStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
			auto Button = [&](int Id, const wchar_t *pLabel, DWORD ButtonStyle) {
				return CreateWindowExW(0, L"BUTTON", pLabel, ControlStyle | ButtonStyle, 0, 0, 0, 0, m_Window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Id)), Instance, nullptr);
			};
			m_Edit = CreateWindowExW(0, L"EDIT", m_Directory.c_str(), ControlStyle | ES_AUTOHSCROLL, 0, 0, 0, 0, m_Window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_DIRECTORY)), Instance, nullptr);
			SendMessageW(m_Edit, EM_LIMITTEXT, 32760, 0);
			m_Browse = Button(ID_BROWSE, L"浏览…", BS_OWNERDRAW);
			m_Desktop = Button(ID_DESKTOP, L"创建桌面快捷方式", BS_AUTOCHECKBOX);
			// 保留原生勾选、键盘与可访问性语义，通过自定义绘制统一外观。
			SetWindowTheme(m_Desktop, L"Explorer", nullptr);
			SendMessageW(m_Desktop, BM_SETCHECK, DesktopIcon ? BST_CHECKED : BST_UNCHECKED, 0);
			m_Check.Snap(DesktopIcon ? 1.0f : 0.0f);
			m_Primary = Button(IDOK, L"开始安装", BS_OWNERDRAW);
			m_Secondary = Button(IDCANCEL, L"取消", BS_OWNERDRAW);
			m_LogButton = Button(ID_LOG, L"查看安装日志", BS_OWNERDRAW);
			m_Status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 0, 0, m_Window, nullptr, Instance, nullptr);
			if(!m_Edit || !m_Browse || !m_Desktop || !m_Primary || !m_Secondary || !m_LogButton || !m_Status)
			{
				DestroyWindow(m_Window);
				return 1;
			}
			size_t FeedbackIndex = 0;
			for(const HWND ButtonWindow : {m_Browse, m_Desktop, m_Primary, m_Secondary, m_LogButton})
			{
				m_aFeedback[FeedbackIndex++].m_Window = ButtonWindow;
				SetWindowSubclass(ButtonWindow, ButtonProc, 1, reinterpret_cast<DWORD_PTR>(this));
			}
			Layout();
			m_Intro.SetTarget(1.0f, m_Animations, 0.40f);
			ShowPage(EPage::OPTIONS);
			ShowWindow(m_Window, Show);
			MSG Message{};
			while(GetMessageW(&Message, nullptr, 0, 0) > 0)
			{
				if(!IsDialogMessageW(m_Window, &Message))
				{
					TranslateMessage(&Message);
					DispatchMessageW(&Message);
				}
			}
			return static_cast<int>(m_ExitCode);
		}
	};
}

int WINAPI wWinMain(HINSTANCE Instance, HINSTANCE, PWSTR, int Show)
{
	int Count = 0;
	wchar_t **ppArguments = CommandLineToArgvW(GetCommandLineW(), &Count);
	if(!ppArguments)
		return 1;
	std::vector<std::wstring> vArguments;
	for(int Index = 1; ppArguments && Index < Count; ++Index)
		vArguments.emplace_back(ppArguments[Index]);
	LocalFree(ppArguments);
	const auto Options = QmSetup::ParseOptions(vArguments);
	if(!Options.m_UseNativeUi)
	{
		QmSetup::CSetupEngine Engine;
		const int Result = static_cast<int>(Engine.Run(Options.m_vArguments));
		if(Result != 0 && !Engine.Error().empty())
			OutputDebugStringW(Engine.Error().c_str());
		return Result;
	}
	const HRESULT ComResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if(FAILED(ComResult))
		return 1;
	INITCOMMONCONTROLSEX Controls{sizeof(Controls), ICC_STANDARD_CLASSES};
	InitCommonControlsEx(&Controls);
	int Result;
	{
		CSetupWindow Window(Options);
		Result = Window.Run(Instance, Show);
	}
	CoUninitialize();
	return Result;
}
