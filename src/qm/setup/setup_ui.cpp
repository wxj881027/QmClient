#include "setup_build.h"
#include "setup_engine.h"
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
#include <cmath>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
	constexpr float WIDTH = 788.0f;
	constexpr float HEIGHT = 506.0f;
	constexpr int ID_BROWSE = 100;
	constexpr int ID_DESKTOP = 101;
	constexpr int ID_LOG = 102;
	constexpr int ID_DIRECTORY = 103;

	D2D1_COLOR_F Color(UINT32 Rgb)
	{
		return D2D1::ColorF(Rgb);
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
		float m_ShownProgress = 0;
		DWORD m_ExitCode = 2;
		std::wstring m_Directory;
		std::wstring m_Error;
		std::unique_ptr<QmSetup::CSetupEngine> m_pEngine;
		ComPtr<ID2D1Factory> m_pFactory;
		ComPtr<IDWriteFactory> m_pWrite;
		ComPtr<ID2D1HwndRenderTarget> m_pTarget;
		ComPtr<ID2D1DCRenderTarget> m_pControlTarget;
		ComPtr<ID2D1Bitmap> m_pLogo;

		UINT32 Background() const { return m_Dark ? 0x171D20 : 0xF7F9F8; }
		UINT32 Foreground() const { return m_Dark ? 0xEDF4F1 : 0x1D302A; }
		UINT32 Muted() const { return m_Dark ? 0xA6B6AF : 0x65786F; }
		UINT32 Border() const { return m_Dark ? 0x3D4B45 : 0xDCE5DF; }
		int Pixels(float Value) const { return static_cast<int>(std::lround(Value * m_Dpi / 96.0f)); }

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

		void Text(ID2D1RenderTarget *pTarget, const std::wstring &Value, D2D1_RECT_F Rect, float Size, UINT32 Rgb, bool Bold = false, bool Center = false)
		{
			ComPtr<IDWriteTextFormat> pFormat;
			ComPtr<ID2D1SolidColorBrush> pBrush;
			if(FAILED(m_pWrite->CreateTextFormat(L"Microsoft YaHei UI", nullptr, Bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, Size, L"zh-CN", &pFormat)) || FAILED(pTarget->CreateSolidColorBrush(Color(Rgb), &pBrush)))
				return;
			if(Center)
			{
				pFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
				pFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
			}
			pTarget->DrawText(Value.c_str(), static_cast<UINT32>(Value.size()), pFormat.Get(), Rect, pBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
		}

		void ReadTheme()
		{
			DWORD Light = 1;
			DWORD Bytes = sizeof(Light);
			RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &Light, &Bytes);
			m_Dark = Light == 0;
			if(m_BackBrush)
				DeleteObject(m_BackBrush);
			const auto Bg = Background();
			m_BackBrush = CreateSolidBrush(RGB((Bg >> 16) & 255, (Bg >> 8) & 255, Bg & 255));
			const BOOL Dark = m_Dark;
			DwmSetWindowAttribute(m_Window, 20, &Dark, sizeof(Dark));
			RedrawWindow(m_Window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
		}

		void Place(HWND Control, float X, float Y, float Width, float Height)
		{
			MoveWindow(Control, Pixels(X), Pixels(Y), Pixels(Width), Pixels(Height), TRUE);
		}

		void Layout()
		{
			const HFONT OldFont = m_Font;
			m_Font = CreateFontW(-Pixels(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
			for(const HWND Control : {m_Edit, m_Browse, m_Desktop, m_Primary, m_Secondary, m_LogButton, m_Status})
				SendMessageW(Control, WM_SETFONT, reinterpret_cast<WPARAM>(m_Font), TRUE);
			if(OldFont)
				DeleteObject(OldFont);
			Place(m_Edit, 270, 208, 354, 24);
			Place(m_Browse, 648, 196, 106, 46);
			Place(m_Desktop, 258, 264, 360, 32);
			Place(m_Primary, 558, 424, 196, 46);
			Place(m_Secondary, 258, 424, 116, 46);
			Place(m_LogButton, 258, 332, 138, 38);
			Place(m_Status, 258, 106, 496, m_Page == EPage::FAILED ? 198 : 52);
		}

		void ShowPage(EPage Page)
		{
			m_Page = Page;
			const bool Options = Page == EPage::OPTIONS;
			for(const HWND Control : {m_Edit, m_Browse, m_Desktop})
				ShowWindow(Control, Options ? SW_SHOW : SW_HIDE);
			ShowWindow(m_Primary, Page == EPage::INSTALLING ? SW_HIDE : SW_SHOW);
			ShowWindow(m_LogButton, Page == EPage::FAILED && m_pEngine && !m_pEngine->Log().empty() ? SW_SHOW : SW_HIDE);
			SetWindowTextW(m_Primary, Page == EPage::DONE ? L"立即启动" : Page == EPage::FAILED ? L"返回安装选项" :
									      m_Upgrade                     ? L"更新安装" :
													      L"开始安装");
			SetWindowTextW(m_Secondary, Page == EPage::DONE || Page == EPage::FAILED ? L"完成" : L"取消");
			const std::wstring Status = Page == EPage::OPTIONS ? (m_Upgrade ? L"已找到现有安装。确认位置后，即可覆盖更新。" : L"准备好后，即可将 QmClient 安装到你的电脑。") : Page == EPage::INSTALLING ? L"安装期间可以最小化此窗口。" :
																						   Page == EPage::DONE               ? L"QmClient 已安装到您的计算机。\n点击“完成”退出安装程序。" :
																										       m_Error;
			SetWindowTextW(m_Status, Status.c_str());
			Place(m_Status, 258, 106, 496, Page == EPage::FAILED ? 198 : 52);
			EnableWindow(m_Secondary, TRUE);
			SetFocus(Page == EPage::INSTALLING ? m_Secondary : m_Primary);
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
			m_Progress = m_ShownProgress = 0;
			m_pEngine = std::make_unique<QmSetup::CSetupEngine>();
			ShowPage(EPage::INSTALLING);
			try
			{
				m_pEngine->Start(QmSetup::NativeArguments(m_Options, Directory, SendMessageW(m_Desktop, BM_GETCHECK, 0, 0) == BST_CHECKED), m_Window);
				SetTimer(m_Window, 1, 33, nullptr);
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
				SetWindowTextW(m_Status, L"正在安全结束安装，请稍候。");
				SetWindowTextW(m_Secondary, L"正在取消");
				EnableWindow(m_Secondary, FALSE);
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
			ComPtr<IWICImagingFactory> pWic;
			ComPtr<IWICBitmap> pBitmap;
			ComPtr<IWICFormatConverter> pConverter;
			if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pWic))) &&
				SUCCEEDED(pWic->CreateBitmapFromHICON(LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1)), &pBitmap)) &&
				SUCCEEDED(pWic->CreateFormatConverter(&pConverter)) &&
				SUCCEEDED(pConverter->Initialize(pBitmap.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
				m_pTarget->CreateBitmapFromWicBitmap(pConverter.Get(), nullptr, &m_pLogo);
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
				Fill(pTarget, {0, 0, 226, HEIGHT}, 0x16392F);
				Fill(pTarget, {24, 34, 102, 112}, 0x275143, 20);
				if(m_pLogo)
					pTarget->DrawBitmap(m_pLogo.Get(), {35, 45, 91, 101});
				Text(pTarget, L"QmClient", {26, 133, 212, 174}, 28, 0xFFFFFF, true);
				Text(pTarget, QM_SETUP_VERSION, {28, 179, 210, 208}, 13, 0xADC9BB);
				Text(pTarget, L"下一段旅程，\n从这里开始。", {28, 232, 206, 298}, 18, 0xD7E8E0);
				const wchar_t *apSteps[] = {L"安装选项", L"正在安装", L"开始使用"};
				const int Active = m_Page == EPage::OPTIONS ? 0 : m_Page == EPage::DONE ? 2 :
													  1;
				for(int Index = 0; Index < 3; ++Index)
				{
					const float Y = 348.0f + Index * 38.0f;
					Fill(pTarget, {28, Y, 52, Y + 24}, Active == Index ? 0xC2E9A0 : 0x2A4B3E, 12);
					Text(pTarget, std::to_wstring(Index + 1), {28, Y, 52, Y + 24}, 12, Active == Index ? 0x16392F : 0xB9CFC2, true, true);
					Text(pTarget, apSteps[Index], {64, Y + 1, 208, Y + 24}, 13, Active == Index ? 0xFFFFFF : 0xADC9BB);
				}
				if(m_Page == EPage::OPTIONS)
				{
					Text(pTarget, m_Upgrade ? L"更新你的 QmClient" : L"安装 QmClient", {258, 47, 754, 94}, 28, Foreground(), true);
					Text(pTarget, L"安装位置", {258, 167, 754, 191}, 13, Foreground(), true);
					Stroke(pTarget, {258, 196, 636, 242}, Border(), 9);
					Text(pTarget, L"完整离线安装 · 无需额外下载", {258, 327, 754, 354}, 13, Muted());
					Text(pTarget, L"玩家存档继续使用系统用户目录。", {258, 359, 754, 385}, 12, Muted());
					Text(pTarget, L"开始前请退出正在运行的客户端。", {258, 386, 754, 410}, 12, Muted());
				}
				else if(m_Page == EPage::INSTALLING)
				{
					Text(pTarget, m_CancelRequested ? L"正在取消安装" : L"正在安装 QmClient", {258, 47, 754, 94}, 28, Foreground(), true);
					Text(pTarget, m_Progress > 0 ? std::to_wstring(std::min(99, static_cast<int>(m_Progress * 100))) + L"%" : L"准备中", {258, 197, 754, 263}, 42, Foreground(), true);
					Fill(pTarget, {258, 291, 754, 301}, Border(), 5);
					if(m_ShownProgress > 0)
						Fill(pTarget, {258, 291, 258 + 496 * m_ShownProgress, 301}, 0x409B69, 5);
					else
					{
						const float Offset = static_cast<float>(GetTickCount64() % 1600) / 1600.0f * 396;
						Fill(pTarget, {258 + Offset, 291, 358 + Offset, 301}, 0x409B69, 5);
					}
					Text(pTarget, m_EngineWindow ? L"正在处理程序文件、快捷方式和卸载信息。" : L"正在准备安装文件…", {258, 324, 754, 372}, 14, Muted());
				}
				else if(m_Page == EPage::DONE)
				{
					Text(pTarget, L"安装完成", {258, 47, 754, 94}, 28, Foreground(), true);
					Fill(pTarget, {258, 184, 754, 284}, m_Dark ? 0x233C2D : 0xE7F2E2, 14);
					Text(pTarget, L"准备就绪", {280, 204, 731, 237}, 21, m_Dark ? 0xBEE7A7 : 0x2C6345, true);
					Text(pTarget, L"可以立即启动，也可以稍后从开始菜单打开。", {280, 246, 731, 273}, 12, Muted());
					Text(pTarget, L"安装位置：" + m_Directory, {258, 327, 754, 404}, 12, Muted());
				}
				else
				{
					Text(pTarget, QmSetup::IsCancelled(m_ExitCode, m_CancelRequested) ? L"安装已取消" : L"安装未完成", {258, 47, 754, 94}, 28, Foreground(), true);
				}
				if(pTarget->EndDraw() == D2DERR_RECREATE_TARGET)
				{
					m_pLogo.Reset();
					m_pTarget.Reset();
				}
			}
			EndPaint(m_Window, &PaintInfo);
		}

		void DrawButton(const DRAWITEMSTRUCT &Item)
		{
			if(!m_pControlTarget)
			{
				auto Properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
				if(FAILED(m_pFactory->CreateDCRenderTarget(&Properties, &m_pControlTarget)))
					return;
			}
			if(FAILED(m_pControlTarget->BindDC(Item.hDC, &Item.rcItem)))
				return;
			auto *pTarget = m_pControlTarget.Get();
			pTarget->SetDpi(static_cast<float>(m_Dpi), static_cast<float>(m_Dpi));
			const float Width = (Item.rcItem.right - Item.rcItem.left) * 96.0f / m_Dpi;
			const float Height = (Item.rcItem.bottom - Item.rcItem.top) * 96.0f / m_Dpi;
			const bool Primary = Item.CtlID == IDOK;
			const bool Pressed = (Item.itemState & ODS_SELECTED) != 0;
			const bool Disabled = (Item.itemState & ODS_DISABLED) != 0;
			const bool Hover = !Disabled && m_HoverButton == Item.hwndItem;
			wchar_t aCaption[128]{};
			GetWindowTextW(Item.hwndItem, aCaption, 128);
			pTarget->BeginDraw();
			pTarget->Clear(Color(Background()));
			const UINT32 ButtonColor = Primary ? (Pressed ? 0x21583C : Hover ? 0x3B8A5D :
											   0x307A50) :
						   Pressed ? Border() :
						   Hover   ? (m_Dark ? 0x29362E : 0xEAF0EB) :
							     Background();
			Fill(pTarget, {1, 1, Width - 1, Height - 1}, ButtonColor, 9);
			if(!Primary)
				Stroke(pTarget, {1, 1, Width - 1, Height - 1}, Border(), 9);
			Text(pTarget, aCaption, {5, 1, Width - 5, Height - 1}, 14, Disabled ? Muted() : Primary ? 0xFFFFFF :
														  Foreground(),
				Primary, true);
			if(Item.itemState & ODS_FOCUS)
				Stroke(pTarget, {4, 4, Width - 4, Height - 4}, Primary ? 0xD6EDBB : 0x409B69, 6);
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
				if(HIWORD(WParam) == BN_CLICKED)
					Activate(LOWORD(WParam));
				return 0;
			case WM_DRAWITEM: DrawButton(*reinterpret_cast<DRAWITEMSTRUCT *>(LParam)); return TRUE;
			case WM_CTLCOLOREDIT:
			case WM_CTLCOLORSTATIC:
			case WM_CTLCOLORBTN:
			{
				const auto Fg = Foreground();
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
				return 0;
			case WM_TIMER:
				m_ShownProgress += (m_Progress - m_ShownProgress) * 0.2f;
				InvalidateRect(m_Window, nullptr, FALSE);
				return 0;
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
					m_Progress = std::clamp(static_cast<float>(WParam) / static_cast<float>(LParam), 0.0f, 0.99f);
				return 0;
			case QmSetup::WM_ENGINE_DONE:
				if(m_Page == EPage::INSTALLING && m_pEngine)
				{
					m_pEngine->Join();
					KillTimer(m_Window, 1);
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
			case WM_DESTROY: PostQuitMessage(static_cast<int>(m_ExitCode)); return 0;
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
			if(Message == WM_MOUSEMOVE && pSelf->m_HoverButton != Window)
			{
				if(pSelf->m_HoverButton)
					InvalidateRect(pSelf->m_HoverButton, nullptr, FALSE);
				pSelf->m_HoverButton = Window;
				TRACKMOUSEEVENT Track{sizeof(Track), TME_LEAVE, Window, 0};
				TrackMouseEvent(&Track);
				InvalidateRect(Window, nullptr, FALSE);
			}
			else if(Message == WM_MOUSELEAVE || Message == WM_NCDESTROY)
			{
				if(pSelf->m_HoverButton == Window)
					pSelf->m_HoverButton = nullptr;
				InvalidateRect(Window, nullptr, FALSE);
				if(Message == WM_NCDESTROY)
					RemoveWindowSubclass(Window, ButtonProc, 1);
			}
			return DefSubclassProc(Window, Message, WParam, LParam);
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
			m_Edit = CreateWindowExW(0, L"EDIT", m_Directory.c_str(), ControlStyle | ES_AUTOHSCROLL, 0, 0, 0, 0, m_Window, reinterpret_cast<HMENU>(ID_DIRECTORY), Instance, nullptr);
			SendMessageW(m_Edit, EM_LIMITTEXT, 32760, 0);
			m_Browse = Button(ID_BROWSE, L"浏览…", BS_OWNERDRAW);
			m_Desktop = Button(ID_DESKTOP, L"创建桌面快捷方式", BS_AUTOCHECKBOX);
			// 复选框保留原生语义，文字和底色由当前窗口主题提供。
			SetWindowTheme(m_Desktop, L"", L"");
			SendMessageW(m_Desktop, BM_SETCHECK, DesktopIcon ? BST_CHECKED : BST_UNCHECKED, 0);
			m_Primary = Button(IDOK, L"开始安装", BS_OWNERDRAW);
			m_Secondary = Button(IDCANCEL, L"取消", BS_OWNERDRAW);
			m_LogButton = Button(ID_LOG, L"查看安装日志", BS_OWNERDRAW);
			m_Status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 0, 0, m_Window, nullptr, Instance, nullptr);
			if(!m_Edit || !m_Browse || !m_Desktop || !m_Primary || !m_Secondary || !m_LogButton || !m_Status)
			{
				DestroyWindow(m_Window);
				return 1;
			}
			for(const HWND ButtonWindow : {m_Browse, m_Primary, m_Secondary, m_LogButton})
				SetWindowSubclass(ButtonWindow, ButtonProc, 1, reinterpret_cast<DWORD_PTR>(this));
			Layout();
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
