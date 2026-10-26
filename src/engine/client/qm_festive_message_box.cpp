#include "qm_festive_message_box.h"

#include "qm_festive_report_text.h"

#include <base/detect.h>

#if defined(CONF_FAMILY_WINDOWS)

#include <base/windows.h>

#if defined(NOGDI)
#undef NOGDI
#endif
#include <windows.h>

#include <mmsystem.h>
#include <opusfile.h>
#include <wingdi.h>

#if !defined(WM_DPICHANGED)
#define WM_DPICHANGED 0x02E0
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace
{
	constexpr wchar_t gs_aWindowClassName[] = L"QmClientFestiveCrashWindow";
	constexpr int gs_ButtonIdBase = 4100;
	constexpr UINT gs_FireworksTimerId = 1;
	constexpr UINT gs_FireworksTimerPeriodMs = 33;
	constexpr int gs_FireworksFrameCount = 270;
	constexpr int gs_FireworksParticleCount = 192;
	constexpr int gs_FireworksParticlesPerBurst = 24;
	constexpr int gs_FireworksBurstDuration = 62;

	COLORREF Rgb(int Red, int Green, int Blue)
	{
		return RGB(Red, Green, Blue);
	}

	int ScaleDip(int Value, unsigned Dpi)
	{
		return MulDiv(Value, static_cast<int>(Dpi), 96);
	}

	bool UpdateWindowCornerRegion(HWND Window, unsigned Dpi)
	{
		RECT ClientRect{};
		if(Window == nullptr || !GetClientRect(Window, &ClientRect) || ClientRect.right <= 0 || ClientRect.bottom <= 0)
			return false;
		const int Radius = std::max(ScaleDip(28, Dpi), 16);
		HRGN Region = CreateRoundRectRgn(0, 0, ClientRect.right + 1, ClientRect.bottom + 1, Radius, Radius);
		if(Region == nullptr)
			return false;
		if(SetWindowRgn(Window, Region, TRUE) == 0)
		{
			DeleteObject(Region);
			return false;
		}
		return true;
	}

	unsigned GetWindowDpiCompat(HWND Window)
	{
		using TGetDpiForWindow = UINT(WINAPI *)(HWND);
		static const auto pGetDpiForWindow = reinterpret_cast<TGetDpiForWindow>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
		if(pGetDpiForWindow != nullptr && Window != nullptr)
			return pGetDpiForWindow(Window);

		HDC DeviceContext = GetDC(Window);
		const int Dpi = DeviceContext != nullptr ? GetDeviceCaps(DeviceContext, LOGPIXELSX) : 96;
		if(DeviceContext != nullptr)
			ReleaseDC(Window, DeviceContext);
		return Dpi > 0 ? static_cast<unsigned>(Dpi) : 96;
	}

	bool AdjustWindowRectForDpiCompat(RECT &WindowRect, DWORD Style, DWORD ExtendedStyle, unsigned Dpi)
	{
		using TAdjustWindowRectExForDpi = BOOL(WINAPI *)(LPRECT, DWORD, BOOL, DWORD, UINT);
		static const auto pAdjustWindowRectExForDpi = reinterpret_cast<TAdjustWindowRectExForDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
		if(pAdjustWindowRectExForDpi != nullptr)
			return pAdjustWindowRectExForDpi(&WindowRect, Style, FALSE, ExtendedStyle, Dpi) != FALSE;
		return AdjustWindowRectEx(&WindowRect, Style, FALSE, ExtendedStyle) != FALSE;
	}

	class CDpiAwarenessGuard
	{
		using TSetThreadDpiAwarenessContext = HANDLE(WINAPI *)(HANDLE);
		TSetThreadDpiAwarenessContext m_pSetThreadDpiAwarenessContext = nullptr;
		HANDLE m_PreviousContext = nullptr;

	public:
		CDpiAwarenessGuard()
		{
			m_pSetThreadDpiAwarenessContext = reinterpret_cast<TSetThreadDpiAwarenessContext>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext"));
			if(m_pSetThreadDpiAwarenessContext != nullptr)
			{
				// DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 的公开句柄值为 -4。
				m_PreviousContext = m_pSetThreadDpiAwarenessContext(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));
			}
		}

		~CDpiAwarenessGuard()
		{
			if(m_pSetThreadDpiAwarenessContext != nullptr && m_PreviousContext != nullptr)
				m_pSetThreadDpiAwarenessContext(m_PreviousContext);
		}
	};

	// 测试专用：进程级回归测试通过该环境变量要求隐藏窗口，避免桌面上的真实点击
	// 提前关闭弹窗、掩盖“弹窗期间看门狗不应触发”的回归。正常运行不会设置它。
	bool FestiveDialogHiddenForTest()
	{
		char aValue[8] = "";
		return GetEnvironmentVariableA("QMCLIENT_TEST_HIDE_DIALOG", aValue, sizeof(aValue)) > 0;
	}

	// 报告内容可能被外部工具截断在多字节序列中间。windows_utf8_to_wide 遇到非法
	// UTF-8 会触发断言并直接终止报告进程，这里必须宽松降级：非法字节按替换字符
	// 显示，保证弹窗仍能打开并展示可用信息。
	std::wstring LenientUtf8ToWide(const char *pText)
	{
		if(pText == nullptr || pText[0] == '\0')
			return {};
		const int WideLength = MultiByteToWideChar(CP_UTF8, 0, pText, -1, nullptr, 0);
		if(WideLength <= 1)
			return {};
		std::wstring Result(static_cast<size_t>(WideLength - 1), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, pText, -1, Result.data(), WideLength);
		return Result;
	}

	std::wstring FestiveButtonLabel(const char *pLabel)
	{
		if(pLabel == nullptr)
			return L"";
		if(strcmp(pLabel, "Show Wiki") == 0)
			return L"查看排障指南";
		if(strcmp(pLabel, "Show crash reports") == 0)
			return L"打开崩溃报告";
		if(strcmp(pLabel, "OK") == 0)
			return L"平安退出";
		if(strcmp(pLabel, "Close report") == 0)
			return L"关闭报告";
		return LenientUtf8ToWide(pLabel);
	}

	struct SFestiveDialogState
	{
		struct SFireworkParticle
		{
			float m_Anchor = 0.0f;
			float m_Angle = 0.0f;
			int m_Delay = 0;
			float m_Speed = 1.0f;
			int m_Size = 2;
			COLORREF m_Color = RGB(255, 205, 82);
		};

		HWND m_Window = nullptr;
		HWND m_Details = nullptr;
		std::vector<HWND> m_vButtons;
		std::vector<std::wstring> m_vButtonLabels;
		std::wstring m_Title;
		std::wstring m_Subtitle;
		std::wstring m_DetailsText;
		unsigned m_Dpi = 96;
		HFONT m_TitleFont = nullptr;
		HFONT m_SubtitleFont = nullptr;
		HFONT m_SectionFont = nullptr;
		HFONT m_BodyFont = nullptr;
		HFONT m_ButtonFont = nullptr;
		HBRUSH m_DetailsBrush = nullptr;
		int m_ConfirmButtonId = -1;
		int m_CancelButtonId = -1;
		int m_Result = -1;
		int m_FireworksFrame = -1;
		std::array<SFireworkParticle, gs_FireworksParticleCount> m_aFireworkParticles{};
		bool m_FireworkParticlesReady = false;
		bool m_Running = true;

		~SFestiveDialogState()
		{
			// 清理窗口时回调仍借用本对象，必须先于字体、文本和按钮状态析构。
			const HWND Window = m_Window;
			if(Window != nullptr && !DestroyWindow(Window))
				SetWindowLongPtrW(Window, GWLP_USERDATA, 0);
			DeleteObject(m_TitleFont);
			DeleteObject(m_SubtitleFont);
			DeleteObject(m_SectionFont);
			DeleteObject(m_BodyFont);
			DeleteObject(m_ButtonFont);
			DeleteObject(m_DetailsBrush);
		}
	};

	HFONT CreateUiFont(unsigned Dpi, int SizeDip, int Weight)
	{
		LOGFONTW Font{};
		Font.lfHeight = -ScaleDip(SizeDip, Dpi);
		Font.lfWeight = Weight;
		Font.lfQuality = CLEARTYPE_QUALITY;
		wcscpy_s(Font.lfFaceName, L"Microsoft YaHei UI");
		return CreateFontIndirectW(&Font);
	}

	void RecreateFonts(SFestiveDialogState &State)
	{
		DeleteObject(State.m_TitleFont);
		DeleteObject(State.m_SubtitleFont);
		DeleteObject(State.m_SectionFont);
		DeleteObject(State.m_BodyFont);
		DeleteObject(State.m_ButtonFont);
		State.m_TitleFont = CreateUiFont(State.m_Dpi, 28, FW_BOLD);
		State.m_SubtitleFont = CreateUiFont(State.m_Dpi, 14, FW_NORMAL);
		State.m_SectionFont = CreateUiFont(State.m_Dpi, 13, FW_BOLD);
		State.m_BodyFont = CreateUiFont(State.m_Dpi, 11, FW_NORMAL);
		State.m_ButtonFont = CreateUiFont(State.m_Dpi, 11, FW_BOLD);
		if(State.m_Details != nullptr)
			SendMessageW(State.m_Details, WM_SETFONT, reinterpret_cast<WPARAM>(State.m_BodyFont), TRUE);
		for(HWND Button : State.m_vButtons)
			SendMessageW(Button, WM_SETFONT, reinterpret_cast<WPARAM>(State.m_ButtonFont), TRUE);
	}

	void LayoutDialog(SFestiveDialogState &State)
	{
		if(State.m_Window == nullptr || State.m_Details == nullptr)
			return;

		RECT ClientRect;
		GetClientRect(State.m_Window, &ClientRect);
		const int Width = ClientRect.right - ClientRect.left;
		const int Height = ClientRect.bottom - ClientRect.top;
		const int Margin = ScaleDip(28, State.m_Dpi);
		const int HeaderHeight = ScaleDip(176, State.m_Dpi);
		const int SectionHeight = ScaleDip(30, State.m_Dpi);
		const int ButtonHeight = ScaleDip(44, State.m_Dpi);
		const int ButtonGap = ScaleDip(12, State.m_Dpi);
		const int FooterHeight = ScaleDip(34, State.m_Dpi);
		const int ButtonTop = Height - Margin - ButtonHeight;
		const int DetailsTop = HeaderHeight + SectionHeight;
		const int DetailsBottom = ButtonTop - FooterHeight;
		MoveWindow(State.m_Details, Margin, DetailsTop, std::max(1, Width - Margin * 2), std::max(1, DetailsBottom - DetailsTop), TRUE);
		SendMessageW(State.m_Details, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(ScaleDip(12, State.m_Dpi), ScaleDip(12, State.m_Dpi)));

		const int ButtonCount = static_cast<int>(State.m_vButtons.size());
		if(ButtonCount == 0)
			return;
		const int AvailableWidth = std::max(1, Width - Margin * 2 - ButtonGap * (ButtonCount - 1));
		const int ButtonWidth = std::min(ScaleDip(184, State.m_Dpi), AvailableWidth / ButtonCount);
		const int ButtonsWidth = ButtonWidth * ButtonCount + ButtonGap * (ButtonCount - 1);
		int ButtonLeft = Width - Margin - ButtonsWidth;
		for(HWND Button : State.m_vButtons)
		{
			MoveWindow(Button, ButtonLeft, ButtonTop, ButtonWidth, ButtonHeight, TRUE);
			ButtonLeft += ButtonWidth + ButtonGap;
		}
	}

	void DrawLantern(HDC DeviceContext, int CenterX, int Top, int Size, COLORREF Red, COLORREF Gold)
	{
		HPEN GoldPen = CreatePen(PS_SOLID, std::max(1, Size / 24), Gold);
		HBRUSH RedBrush = CreateSolidBrush(Red);
		HGDIOBJ PreviousPen = SelectObject(DeviceContext, GoldPen);
		HGDIOBJ PreviousBrush = SelectObject(DeviceContext, RedBrush);
		MoveToEx(DeviceContext, CenterX, Top - Size / 3, nullptr);
		LineTo(DeviceContext, CenterX, Top);
		RoundRect(DeviceContext, CenterX - Size / 2, Top, CenterX + Size / 2, Top + Size, Size / 2, Size / 2);
		MoveToEx(DeviceContext, CenterX, Top + Size, nullptr);
		LineTo(DeviceContext, CenterX, Top + Size + Size / 3);
		MoveToEx(DeviceContext, CenterX - Size / 5, Top + Size + Size / 7, nullptr);
		LineTo(DeviceContext, CenterX, Top + Size + Size / 3);
		LineTo(DeviceContext, CenterX + Size / 5, Top + Size + Size / 7);
		SelectObject(DeviceContext, PreviousBrush);
		SelectObject(DeviceContext, PreviousPen);
		DeleteObject(RedBrush);
		DeleteObject(GoldPen);
	}

	void PrepareFireworkParticles(SFestiveDialogState &State)
	{
		if(State.m_FireworkParticlesReady)
			return;
		const COLORREF aColors[] = {Rgb(255, 232, 142), Rgb(255, 112, 92), Rgb(255, 205, 82), Rgb(255, 244, 194)};
		for(int Index = 0; Index < gs_FireworksParticleCount; ++Index)
		{
			SFestiveDialogState::SFireworkParticle &Particle = State.m_aFireworkParticles[Index];
			const int Burst = Index / gs_FireworksParticlesPerBurst;
			const int ParticleIndex = Index % gs_FireworksParticlesPerBurst;
			Particle.m_Anchor = 0.06f + Burst * 0.16f;
			Particle.m_Angle = 6.28318530718f * ParticleIndex / gs_FireworksParticlesPerBurst + Burst * 0.23f;
			Particle.m_Delay = Burst * 28 + ParticleIndex % 4;
			Particle.m_Speed = 0.88f + (ParticleIndex % 7) * 0.045f;
			Particle.m_Size = 2 + ParticleIndex % 3;
			Particle.m_Color = aColors[Index % std::size(aColors)];
		}
		State.m_FireworkParticlesReady = true;
	}

	struct SPerimeterPoint
	{
		float m_X = 0.0f;
		float m_Y = 0.0f;
	};

	SPerimeterPoint PerimeterPoint(float Progress, int Width, int Height, int Inset, int Radius)
	{
		const float Pi = 3.14159265359f;
		const float ArcLength = Pi * Radius / 2.0f;
		const float StraightHorizontal = static_cast<float>(Width - 2 * Inset - 2 * Radius);
		const float StraightVertical = static_cast<float>(Height - 2 * Inset - 2 * Radius);
		const float Perimeter = 2.0f * (StraightHorizontal + StraightVertical) + 4.0f * ArcLength;
		float Distance = std::fmod(std::max(0.0f, Progress), 1.0f) * Perimeter;
		const float Left = static_cast<float>(Inset);
		const float Top = static_cast<float>(Inset);
		const float Right = static_cast<float>(Width - Inset);
		const float Bottom = static_cast<float>(Height - Inset);
		if(Distance < StraightHorizontal)
			return {Left + Radius + Distance, Top};
		Distance -= StraightHorizontal;
		if(Distance < ArcLength)
		{
			const float Angle = -Pi / 2.0f + Distance / Radius;
			return {Right - Radius + std::cos(Angle) * Radius, Top + Radius + std::sin(Angle) * Radius};
		}
		Distance -= ArcLength;
		if(Distance < StraightVertical)
			return {Right, Top + Radius + Distance};
		Distance -= StraightVertical;
		if(Distance < ArcLength)
		{
			const float Angle = Distance / Radius;
			return {Right - Radius + std::cos(Angle) * Radius, Bottom - Radius + std::sin(Angle) * Radius};
		}
		Distance -= ArcLength;
		if(Distance < StraightHorizontal)
			return {Right - Radius - Distance, Bottom};
		Distance -= StraightHorizontal;
		if(Distance < ArcLength)
		{
			const float Angle = Pi / 2.0f + Distance / Radius;
			return {Left + Radius + std::cos(Angle) * Radius, Bottom - Radius + std::sin(Angle) * Radius};
		}
		Distance -= ArcLength;
		if(Distance < StraightVertical)
			return {Left, Bottom - Radius - Distance};
		Distance -= StraightVertical;
		const float Angle = Pi + Distance / Radius;
		return {Left + Radius + std::cos(Angle) * Radius, Top + Radius + std::sin(Angle) * Radius};
	}

	void DrawFireworks(const SFestiveDialogState &State, HDC DeviceContext, int Width, int Height)
	{
		if(State.m_FireworksFrame < 0)
			return;

		const int Inset = ScaleDip(16, State.m_Dpi);
		const int Radius = ScaleDip(24, State.m_Dpi);
		for(const SFestiveDialogState::SFireworkParticle &Particle : State.m_aFireworkParticles)
		{
			const int Age = State.m_FireworksFrame - Particle.m_Delay;
			if(Age < 0 || Age > gs_FireworksBurstDuration)
				continue;
			const float Progress = Age / static_cast<float>(gs_FireworksBurstDuration);
			const float EasedProgress = 1.0f - (1.0f - Progress) * (1.0f - Progress);
			const float RadiusNow = ScaleDip(5, State.m_Dpi) + ScaleDip(62, State.m_Dpi) * EasedProgress * Particle.m_Speed;
			const float RadiusPrevious = ScaleDip(5, State.m_Dpi) + ScaleDip(62, State.m_Dpi) * std::max(0.0f, Progress - 0.045f) * Particle.m_Speed;
			const SPerimeterPoint Anchor = PerimeterPoint(Particle.m_Anchor, Width, Height, Inset, Radius);
			const float Cosine = std::cos(Particle.m_Angle);
			const float Sine = std::sin(Particle.m_Angle);
			const int CurrentX = static_cast<int>(Anchor.m_X + Cosine * RadiusNow);
			const int CurrentY = static_cast<int>(Anchor.m_Y + Sine * RadiusNow);
			const int PreviousX = static_cast<int>(Anchor.m_X + Cosine * RadiusPrevious);
			const int PreviousY = static_cast<int>(Anchor.m_Y + Sine * RadiusPrevious);
			const int Size = ScaleDip(Particle.m_Size, State.m_Dpi);
			HPEN TrailPen = CreatePen(PS_SOLID, std::max(1, Size / 2), Particle.m_Color);
			HGDIOBJ PreviousPen = SelectObject(DeviceContext, TrailPen);
			MoveToEx(DeviceContext, PreviousX, PreviousY, nullptr);
			LineTo(DeviceContext, CurrentX, CurrentY);
			SelectObject(DeviceContext, PreviousPen);
			DeleteObject(TrailPen);

			HBRUSH Brush = CreateSolidBrush(Particle.m_Color);
			HGDIOBJ PreviousBrush = SelectObject(DeviceContext, Brush);
			Ellipse(DeviceContext, CurrentX - Size, CurrentY - Size, CurrentX + Size, CurrentY + Size);
			SelectObject(DeviceContext, PreviousBrush);
			DeleteObject(Brush);
		}
	}

	std::wstring FindFestiveMusicPath()
	{
		wchar_t aConfiguredPath[2048]{};
		const DWORD ConfiguredLength = GetEnvironmentVariableW(L"QMCLIENT_FESTIVE_BGM", aConfiguredPath, std::size(aConfiguredPath));
		if(ConfiguredLength > 0 && ConfiguredLength < std::size(aConfiguredPath) && GetFileAttributesW(aConfiguredPath) != INVALID_FILE_ATTRIBUTES)
			return aConfiguredPath;

		wchar_t aModulePath[MAX_PATH]{};
		const DWORD ModuleLength = GetModuleFileNameW(nullptr, aModulePath, std::size(aModulePath));
		if(ModuleLength == 0 || ModuleLength >= std::size(aModulePath))
			return {};
		std::wstring ModuleDirectory(aModulePath, ModuleLength);
		const size_t Separator = ModuleDirectory.find_last_of(L"\\/");
		if(Separator == std::wstring::npos)
			return {};
		ModuleDirectory.resize(Separator);
		for(const wchar_t *pRelativePath : {L"data/audio/crash_music/qm_festive_haoyunlai.opus"})
		{
			const std::wstring Candidate = ModuleDirectory + L"/" + pRelativePath;
			if(GetFileAttributesW(Candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
				return Candidate;
		}
		return {};
	}

	// 崩溃窗口不能复用可能已损坏的 SDL/游戏音频状态；独立解码并交给 WinMM。
	class CFestiveMusicPlayer
	{
		std::atomic<bool> m_Stop{false};
		std::thread m_Worker;

		void Play(const std::wstring &Path)
		{
			FILE *pFile = _wfopen(Path.c_str(), L"rb");
			if(pFile == nullptr)
				return;
			const OpusFileCallbacks Callbacks{
				[](void *pStream, unsigned char *pData, int Size) {
					FILE *pInput = static_cast<FILE *>(pStream);
					const size_t Read = fread(pData, 1, Size, pInput);
					return ferror(pInput) ? -1 : static_cast<int>(Read);
				},
				[](void *pStream, opus_int64 Offset, int Whence) { return _fseeki64(static_cast<FILE *>(pStream), Offset, Whence); },
				[](void *pStream) -> opus_int64 { return _ftelli64(static_cast<FILE *>(pStream)); },
				[](void *pStream) { return fclose(static_cast<FILE *>(pStream)); }};
			OggOpusFile *pDecoder = op_open_callbacks(pFile, &Callbacks, nullptr, 0, nullptr);
			if(pDecoder == nullptr)
			{
				fclose(pFile);
				return;
			}
			// 驱动异常未归还缓冲时需要保留其内存，不能让栈析构造成驱动悬垂访问。
			struct SAudioBuffers
			{
				std::array<std::array<opus_int16, 4096>, 4> m_aSamples{};
				std::array<WAVEHDR, 4> m_aHeaders{};
			};
			std::unique_ptr<SAudioBuffers> pBuffers(new(std::nothrow) SAudioBuffers);
			if(!pBuffers || m_Stop.load())
			{
				op_free(pDecoder);
				return;
			}
			WAVEFORMATEX Format{};
			Format.wFormatTag = WAVE_FORMAT_PCM;
			Format.nChannels = 2;
			Format.nSamplesPerSec = 48000;
			Format.wBitsPerSample = 16;
			Format.nBlockAlign = 4;
			Format.nAvgBytesPerSec = Format.nSamplesPerSec * Format.nBlockAlign;
			HWAVEOUT Output = nullptr;
			if(waveOutOpen(&Output, WAVE_MAPPER, &Format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
			{
				op_free(pDecoder);
				return;
			}
			auto &aSamples = pBuffers->m_aSamples;
			auto &aHeaders = pBuffers->m_aHeaders;
			bool Failed = false;
			int EmptyReads = 0;
			while(!m_Stop.load() && !Failed)
			{
				bool Submitted = false;
				for(size_t Index = 0; Index < aHeaders.size() && !m_Stop.load(); ++Index)
				{
					WAVEHDR &Header = aHeaders[Index];
					if((Header.dwFlags & WHDR_PREPARED) != 0)
					{
						if((Header.dwFlags & WHDR_DONE) == 0)
							continue;
						if(waveOutUnprepareHeader(Output, &Header, sizeof(Header)) != MMSYSERR_NOERROR)
						{
							Failed = true;
							break;
						}
					}
					const int Frames = op_read_stereo(pDecoder, aSamples[Index].data(), static_cast<int>(aSamples[Index].size()));
					if(Frames <= 0)
					{
						// 空文件、损坏流或无法回绕时停止，避免后台忙循环。
						if(Frames < 0 || ++EmptyReads > 1 || op_pcm_seek(pDecoder, 0) != 0)
							Failed = true;
						break;
					}
					EmptyReads = 0;
					Header = {};
					Header.lpData = reinterpret_cast<char *>(aSamples[Index].data());
					Header.dwBufferLength = Frames * Format.nBlockAlign;
					if(waveOutPrepareHeader(Output, &Header, sizeof(Header)) != MMSYSERR_NOERROR ||
						waveOutWrite(Output, &Header, sizeof(Header)) != MMSYSERR_NOERROR)
					{
						Failed = true;
						break;
					}
					Submitted = true;
				}
				if(!Submitted)
					Sleep(5);
			}
			// 先归还驱动持有的缓冲区；最多重试五次（额外等待 25ms），不因 WAVERR_STILLPLAYING 无限轮询。
			bool Released = false;
			for(int Attempt = 0; Attempt < 5 && !Released; ++Attempt)
			{
				waveOutReset(Output);
				Released = true;
				for(WAVEHDR &Header : aHeaders)
					if((Header.dwFlags & WHDR_PREPARED) != 0 && waveOutUnprepareHeader(Output, &Header, sizeof(Header)) != MMSYSERR_NOERROR)
						Released = false;
				if(!Released)
					Sleep(5);
			}
			if(!Released || waveOutClose(Output) != MMSYSERR_NOERROR)
			{
				// 故障驱动仍可能引用头和 PCM：仅此异常路径保留至进程结束，避免 UAF。
				pBuffers.release();
			}
			op_free(pDecoder);
		}

	public:
		~CFestiveMusicPlayer() { Stop(); }
		void Start(const std::wstring &Path)
		{
			Stop();
			if(Path.empty())
				return;
			m_Stop.store(false);
			try
			{
				m_Worker = std::thread([this, Path] { Play(Path); });
			}
			catch(...)
			{
				// 线程资源不足时仍允许用户操作崩溃报告窗口。
			}
		}
		void Stop()
		{
			m_Stop.store(true);
			if(m_Worker.joinable())
				m_Worker.join();
		}
	};

	CFestiveMusicPlayer gs_MusicPlayer;

	void StartFestiveMusic() noexcept
	{
		try
		{
			gs_MusicPlayer.Start(FindFestiveMusicPath());
		}
		catch(...)
		{
			// 可选音乐路径准备失败时保留已经显示的报告窗口。
		}
	}

	void StopFestiveMusic()
	{
		gs_MusicPlayer.Stop();
	}

	// 只让边框环带进入重绘区域，正文 EDIT 和按钮不会随动画反复重画。
	void InvalidateFireworksRegion(HWND Window, const SFestiveDialogState &State, int Width, int Height)
	{
		const int OuterRadius = ScaleDip(40, State.m_Dpi);
		const int InnerInset = ScaleDip(92, State.m_Dpi);
		const int InnerRadius = std::max(ScaleDip(4, State.m_Dpi), OuterRadius - InnerInset);
		HRGN Outer = CreateRoundRectRgn(0, 0, Width + 1, Height + 1, OuterRadius, OuterRadius);
		HRGN Inner = CreateRoundRectRgn(InnerInset, InnerInset, Width - InnerInset, Height - InnerInset, InnerRadius, InnerRadius);
		HRGN Border = CreateRectRgn(0, 0, 0, 0);
		if(Outer == nullptr || Inner == nullptr || Border == nullptr)
		{
			// 任一区域创建失败时跳过本帧：Border 为 NULL 会让 InvalidateRgn
			// 失效整个客户区，在 GDI 资源紧张的崩溃场景里造成 33ms 一次的全窗重绘。
			if(Border != nullptr)
				DeleteObject(Border);
			if(Inner != nullptr)
				DeleteObject(Inner);
			if(Outer != nullptr)
				DeleteObject(Outer);
			return;
		}
		CombineRgn(Border, Outer, Inner, RGN_DIFF);
		InvalidateRgn(Window, Border, FALSE);
		DeleteObject(Border);
		DeleteObject(Inner);
		DeleteObject(Outer);
	}

	void PaintDialog(SFestiveDialogState &State)
	{
		PAINTSTRUCT Paint;
		HDC WindowDeviceContext = BeginPaint(State.m_Window, &Paint);
		RECT ClientRect;
		GetClientRect(State.m_Window, &ClientRect);
		const int Width = ClientRect.right;
		const int Height = ClientRect.bottom;
		if(Width <= 0 || Height <= 0)
		{
			EndPaint(State.m_Window, &Paint);
			return;
		}

		// 先在离屏位图中完成整帧，再一次性拷贝到失效区域，避免 GDI
		// 在真实窗口上暴露“清背景 -> 重画文字/控件”的中间状态。
		HDC DeviceContext = CreateCompatibleDC(WindowDeviceContext);
		HBITMAP BackBuffer = CreateCompatibleBitmap(WindowDeviceContext, Width, Height);
		if(DeviceContext == nullptr || BackBuffer == nullptr)
		{
			if(BackBuffer != nullptr)
				DeleteObject(BackBuffer);
			if(DeviceContext != nullptr)
				DeleteDC(DeviceContext);
			EndPaint(State.m_Window, &Paint);
			return;
		}
		HGDIOBJ PreviousBitmap = SelectObject(DeviceContext, BackBuffer);
		const int Margin = ScaleDip(28, State.m_Dpi);
		const COLORREF DeepRed = Rgb(88, 4, 13);
		const COLORREF HeaderRed = Rgb(174, 16, 31);
		const COLORREF PanelRed = Rgb(66, 5, 12);
		const COLORREF Gold = Rgb(255, 205, 82);
		const COLORREF PaleGold = Rgb(255, 240, 194);

		HBRUSH BackgroundBrush = CreateSolidBrush(DeepRed);
		FillRect(DeviceContext, &ClientRect, BackgroundBrush);
		DeleteObject(BackgroundBrush);

		RECT HeaderRect{0, 0, Width, ScaleDip(160, State.m_Dpi)};
		HBRUSH HeaderBrush = CreateSolidBrush(HeaderRed);
		FillRect(DeviceContext, &HeaderRect, HeaderBrush);
		DeleteObject(HeaderBrush);

		HPEN BorderPen = CreatePen(PS_SOLID, std::max(1, ScaleDip(3, State.m_Dpi)), Gold);
		HGDIOBJ PreviousPen = SelectObject(DeviceContext, BorderPen);
		HGDIOBJ PreviousBrush = SelectObject(DeviceContext, GetStockObject(NULL_BRUSH));
		RoundRect(DeviceContext, ScaleDip(8, State.m_Dpi), ScaleDip(8, State.m_Dpi), Width - ScaleDip(8, State.m_Dpi), Height - ScaleDip(8, State.m_Dpi), ScaleDip(18, State.m_Dpi), ScaleDip(18, State.m_Dpi));
		SelectObject(DeviceContext, PreviousBrush);
		SelectObject(DeviceContext, PreviousPen);
		DeleteObject(BorderPen);

		DrawLantern(DeviceContext, Width - ScaleDip(70, State.m_Dpi), ScaleDip(22, State.m_Dpi), ScaleDip(46, State.m_Dpi), Rgb(205, 29, 39), Gold);
		DrawLantern(DeviceContext, Width - ScaleDip(128, State.m_Dpi), ScaleDip(6, State.m_Dpi), ScaleDip(31, State.m_Dpi), Rgb(198, 20, 33), Gold);

		const int EmblemSize = ScaleDip(92, State.m_Dpi);
		RECT EmblemRect{Margin, ScaleDip(34, State.m_Dpi), Margin + EmblemSize, ScaleDip(34, State.m_Dpi) + EmblemSize};
		HBRUSH EmblemBrush = CreateSolidBrush(Gold);
		HPEN EmblemPen = CreatePen(PS_SOLID, std::max(1, ScaleDip(2, State.m_Dpi)), PaleGold);
		PreviousBrush = SelectObject(DeviceContext, EmblemBrush);
		PreviousPen = SelectObject(DeviceContext, EmblemPen);
		Ellipse(DeviceContext, EmblemRect.left, EmblemRect.top, EmblemRect.right, EmblemRect.bottom);
		SelectObject(DeviceContext, PreviousPen);
		SelectObject(DeviceContext, PreviousBrush);
		DeleteObject(EmblemPen);
		DeleteObject(EmblemBrush);

		SetBkMode(DeviceContext, TRANSPARENT);
		SetTextColor(DeviceContext, DeepRed);
		HFONT EmblemFont = CreateUiFont(State.m_Dpi, 42, FW_BOLD);
		HGDIOBJ PreviousFont = SelectObject(DeviceContext, EmblemFont);
		DrawTextW(DeviceContext, L"福", -1, &EmblemRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		SelectObject(DeviceContext, PreviousFont);
		DeleteObject(EmblemFont);

		RECT TitleRect{Margin + EmblemSize + ScaleDip(24, State.m_Dpi), ScaleDip(39, State.m_Dpi), Width - ScaleDip(160, State.m_Dpi), ScaleDip(87, State.m_Dpi)};
		SetTextColor(DeviceContext, PaleGold);
		PreviousFont = SelectObject(DeviceContext, State.m_TitleFont);
		DrawTextW(DeviceContext, State.m_Title.c_str(), -1, &TitleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		SelectObject(DeviceContext, PreviousFont);

		RECT SubtitleRect{TitleRect.left, ScaleDip(92, State.m_Dpi), Width - ScaleDip(150, State.m_Dpi), ScaleDip(128, State.m_Dpi)};
		SetTextColor(DeviceContext, Rgb(255, 225, 159));
		PreviousFont = SelectObject(DeviceContext, State.m_SubtitleFont);
		DrawTextW(DeviceContext, State.m_Subtitle.c_str(), -1, &SubtitleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		SelectObject(DeviceContext, PreviousFont);
		DrawFireworks(State, DeviceContext, Width, Height);

		RECT SectionRect{Margin, ScaleDip(166, State.m_Dpi), Width - Margin, ScaleDip(200, State.m_Dpi)};
		SetTextColor(DeviceContext, Gold);
		PreviousFont = SelectObject(DeviceContext, State.m_SectionFont);
		DrawTextW(DeviceContext, L"故障详情 · 可滚动查看", -1, &SectionRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		SelectObject(DeviceContext, PreviousFont);

		RECT FooterRect{Margin, Height - ScaleDip(112, State.m_Dpi), Width / 2, Height - ScaleDip(72, State.m_Dpi)};
		SetTextColor(DeviceContext, Rgb(230, 176, 91));
		PreviousFont = SelectObject(DeviceContext, State.m_SubtitleFont);
		DrawTextW(DeviceContext, L"别慌，日志还在，好运也在。", -1, &FooterRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		SelectObject(DeviceContext, PreviousFont);

		// 覆盖正文控件外侧可能暴露的空隙，使缩放和调整窗口时保持一致。
		RECT DetailsFrame{Margin - ScaleDip(2, State.m_Dpi), ScaleDip(204, State.m_Dpi), Width - Margin + ScaleDip(2, State.m_Dpi), Height - ScaleDip(116, State.m_Dpi)};
		HBRUSH PanelBrush = CreateSolidBrush(PanelRed);
		FrameRect(DeviceContext, &DetailsFrame, PanelBrush);
		DeleteObject(PanelBrush);

		const int PaintWidth = Paint.rcPaint.right - Paint.rcPaint.left;
		const int PaintHeight = Paint.rcPaint.bottom - Paint.rcPaint.top;
		if(PaintWidth > 0 && PaintHeight > 0)
		{
			BitBlt(WindowDeviceContext, Paint.rcPaint.left, Paint.rcPaint.top, PaintWidth, PaintHeight,
				DeviceContext, Paint.rcPaint.left, Paint.rcPaint.top, SRCCOPY);
		}
		SelectObject(DeviceContext, PreviousBitmap);
		DeleteObject(BackBuffer);
		DeleteDC(DeviceContext);
		EndPaint(State.m_Window, &Paint);
	}

	void DrawButton(const SFestiveDialogState &State, const DRAWITEMSTRUCT &Item)
	{
		const bool Primary = static_cast<int>(Item.CtlID) == State.m_ConfirmButtonId;
		const bool Pressed = (Item.itemState & ODS_SELECTED) != 0;
		const bool Disabled = (Item.itemState & ODS_DISABLED) != 0;
		const COLORREF Gold = Rgb(255, 205, 82);
		const COLORREF Background = Primary ? (Pressed ? Rgb(225, 166, 48) : Gold) : (Pressed ? Rgb(117, 17, 26) : Rgb(91, 8, 17));
		const COLORREF TextColor = Disabled ? Rgb(170, 133, 84) : (Primary ? Rgb(88, 4, 13) : Rgb(255, 230, 169));

		HBRUSH Brush = CreateSolidBrush(Background);
		HPEN Pen = CreatePen(PS_SOLID, std::max(1, ScaleDip(2, State.m_Dpi)), Gold);
		HGDIOBJ PreviousBrush = SelectObject(Item.hDC, Brush);
		HGDIOBJ PreviousPen = SelectObject(Item.hDC, Pen);
		RoundRect(Item.hDC, Item.rcItem.left, Item.rcItem.top, Item.rcItem.right, Item.rcItem.bottom, ScaleDip(12, State.m_Dpi), ScaleDip(12, State.m_Dpi));
		SelectObject(Item.hDC, PreviousPen);
		SelectObject(Item.hDC, PreviousBrush);
		DeleteObject(Pen);
		DeleteObject(Brush);

		wchar_t aLabel[128];
		GetWindowTextW(Item.hwndItem, aLabel, std::size(aLabel));
		RECT TextRect = Item.rcItem;
		SetBkMode(Item.hDC, TRANSPARENT);
		SetTextColor(Item.hDC, TextColor);
		HGDIOBJ PreviousFont = SelectObject(Item.hDC, State.m_ButtonFont);
		DrawTextW(Item.hDC, aLabel, -1, &TextRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		SelectObject(Item.hDC, PreviousFont);
		if((Item.itemState & ODS_FOCUS) != 0)
		{
			RECT FocusRect = Item.rcItem;
			InflateRect(&FocusRect, -ScaleDip(5, State.m_Dpi), -ScaleDip(5, State.m_Dpi));
			DrawFocusRect(Item.hDC, &FocusRect);
		}
	}

	SFestiveDialogState *GetDialogState(HWND Window)
	{
		return reinterpret_cast<SFestiveDialogState *>(GetWindowLongPtrW(Window, GWLP_USERDATA));
	}

	void CloseDialog(SFestiveDialogState &State, int Result)
	{
		State.m_Result = Result;
		State.m_Running = false;
		DestroyWindow(State.m_Window);
	}

	LRESULT CALLBACK FestiveDialogWindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
	{
		if(Message == WM_NCCREATE)
		{
			const auto *pCreate = reinterpret_cast<CREATESTRUCTW *>(LParam);
			SetWindowLongPtrW(Window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreate->lpCreateParams));
		}
		SFestiveDialogState *pState = GetDialogState(Window);
		if(pState == nullptr)
			return DefWindowProcW(Window, Message, WParam, LParam);

		switch(Message)
		{
		case WM_CREATE:
			pState->m_Window = Window;
			pState->m_Dpi = GetWindowDpiCompat(Window);
			pState->m_DetailsBrush = CreateSolidBrush(Rgb(66, 5, 12));
			RecreateFonts(*pState);
			return 0;
		case WM_SIZE:
			UpdateWindowCornerRegion(Window, pState->m_Dpi);
			LayoutDialog(*pState);
			InvalidateRect(Window, nullptr, FALSE);
			return 0;
		case WM_DPICHANGED:
		{
			pState->m_Dpi = HIWORD(WParam);
			const RECT *pSuggestedRect = reinterpret_cast<const RECT *>(LParam);
			SetWindowPos(Window, nullptr, pSuggestedRect->left, pSuggestedRect->top, pSuggestedRect->right - pSuggestedRect->left, pSuggestedRect->bottom - pSuggestedRect->top, SWP_NOACTIVATE | SWP_NOZORDER);
			RecreateFonts(*pState);
			UpdateWindowCornerRegion(Window, pState->m_Dpi);
			LayoutDialog(*pState);
			InvalidateRect(Window, nullptr, TRUE);
			return 0;
		}
		case WM_GETMINMAXINFO:
		{
			auto *pInfo = reinterpret_cast<MINMAXINFO *>(LParam);
			pInfo->ptMinTrackSize.x = ScaleDip(720, pState->m_Dpi);
			pInfo->ptMinTrackSize.y = ScaleDip(520, pState->m_Dpi);
			return 0;
		}
		case WM_COMMAND:
			if(LOWORD(WParam) >= gs_ButtonIdBase && LOWORD(WParam) < gs_ButtonIdBase + static_cast<int>(pState->m_vButtons.size()))
			{
				CloseDialog(*pState, LOWORD(WParam) - gs_ButtonIdBase);
				return 0;
			}
			break;
		case WM_TIMER:
			if(WParam == gs_FireworksTimerId && pState->m_FireworksFrame >= 0)
			{
				++pState->m_FireworksFrame;
				if(pState->m_FireworksFrame >= gs_FireworksFrameCount)
				{
					pState->m_FireworksFrame = -1;
					KillTimer(Window, gs_FireworksTimerId);
				}
				RECT ClientRect;
				GetClientRect(Window, &ClientRect);
				InvalidateFireworksRegion(Window, *pState, ClientRect.right, ClientRect.bottom);
				return 0;
			}
			break;
		case WM_DRAWITEM:
			DrawButton(*pState, *reinterpret_cast<DRAWITEMSTRUCT *>(LParam));
			return TRUE;
		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORSTATIC:
			SetTextColor(reinterpret_cast<HDC>(WParam), Rgb(255, 235, 194));
			SetBkColor(reinterpret_cast<HDC>(WParam), Rgb(66, 5, 12));
			return reinterpret_cast<LRESULT>(pState->m_DetailsBrush);
		case WM_ERASEBKGND:
			return TRUE;
		case WM_PAINT:
			PaintDialog(*pState);
			return 0;
		case WM_CLOSE:
			CloseDialog(*pState, pState->m_CancelButtonId >= gs_ButtonIdBase ? pState->m_CancelButtonId - gs_ButtonIdBase : static_cast<int>(pState->m_vButtons.size()) - 1);
			return 0;
		case WM_NCDESTROY:
			pState->m_Window = nullptr;
			SetWindowLongPtrW(Window, GWLP_USERDATA, 0);
			return DefWindowProcW(Window, Message, WParam, LParam);
		case WM_DESTROY:
			KillTimer(Window, gs_FireworksTimerId);
			StopFestiveMusic();
			pState->m_Running = false;
			return 0;
		default:
			break;
		}
		return DefWindowProcW(Window, Message, WParam, LParam);
	}

	bool RegisterFestiveWindowClass(HINSTANCE Instance)
	{
		WNDCLASSEXW WindowClass{};
		WindowClass.cbSize = sizeof(WindowClass);
		WindowClass.style = CS_HREDRAW | CS_VREDRAW;
		WindowClass.lpfnWndProc = FestiveDialogWindowProc;
		WindowClass.hInstance = Instance;
		WindowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		WindowClass.lpszClassName = gs_aWindowClassName;
		if(RegisterClassExW(&WindowClass) != 0)
			return true;
		return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
	}
}

std::optional<int> ShowQmFestiveMessageBox(const IGraphics::CMessageBox &MessageBox)
{
	try
	{
		CDpiAwarenessGuard DpiAwarenessGuard;
		HINSTANCE Instance = GetModuleHandleW(nullptr);
		if(!RegisterFestiveWindowClass(Instance))
			return std::nullopt;

		SFestiveDialogState State;
		const auto LocalizedReport = QmFestiveReportText::LocalizeCrashReport(
			LenientUtf8ToWide(MessageBox.m_pTitle != nullptr ? MessageBox.m_pTitle : ""),
			LenientUtf8ToWide(MessageBox.m_pMessage != nullptr ? MessageBox.m_pMessage : ""));
		State.m_Title = LocalizedReport.m_Title;
		State.m_Subtitle = LocalizedReport.m_Subtitle;
		State.m_DetailsText = LocalizedReport.m_Details;
		State.m_vButtonLabels.reserve(MessageBox.m_vButtons.size());
		for(const auto &Button : MessageBox.m_vButtons)
			State.m_vButtonLabels.emplace_back(FestiveButtonLabel(Button.m_pLabel));

		POINT CursorPosition{};
		GetCursorPos(&CursorPosition);
		MONITORINFO MonitorInfo{};
		MonitorInfo.cbSize = sizeof(MonitorInfo);
		GetMonitorInfoW(MonitorFromPoint(CursorPosition, MONITOR_DEFAULTTONEAREST), &MonitorInfo);
		const unsigned InitialDpi = GetWindowDpiCompat(nullptr);
		// WS_CLIPCHILDREN: 父窗口不把 EDIT/按钮所在位置再刷一遍深红，避免动画时
		// EDIT/按钮被父窗口覆盖-重画造成的 30Hz 闪烁。
		// 无系统标题栏/边框，窗口内的“关闭报告”按钮是唯一关闭入口。
		const DWORD Style = WS_POPUP | WS_CLIPCHILDREN;
		const DWORD ExtendedStyle = WS_EX_APPWINDOW | WS_EX_CONTROLPARENT;
		RECT WindowRect{0, 0, ScaleDip(940, InitialDpi), ScaleDip(650, InitialDpi)};
		AdjustWindowRectForDpiCompat(WindowRect, Style, ExtendedStyle, InitialDpi);
		int WindowWidth = WindowRect.right - WindowRect.left;
		int WindowHeight = WindowRect.bottom - WindowRect.top;
		const int WorkWidth = MonitorInfo.rcWork.right - MonitorInfo.rcWork.left;
		const int WorkHeight = MonitorInfo.rcWork.bottom - MonitorInfo.rcWork.top;
		WindowWidth = std::min(WindowWidth, WorkWidth * 94 / 100);
		WindowHeight = std::min(WindowHeight, WorkHeight * 94 / 100);
		const int WindowX = MonitorInfo.rcWork.left + (WorkWidth - WindowWidth) / 2;
		const int WindowY = MonitorInfo.rcWork.top + (WorkHeight - WindowHeight) / 2;

		State.m_Window = CreateWindowExW(ExtendedStyle, gs_aWindowClassName, L"QmClient · 好运还在", Style,
			WindowX, WindowY, WindowWidth, WindowHeight, nullptr, nullptr, Instance, &State);
		if(State.m_Window == nullptr)
			return std::nullopt;

		State.m_Details = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", State.m_DetailsText.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
			0, 0, 1, 1, State.m_Window, nullptr, Instance, nullptr);
		if(State.m_Details == nullptr)
		{
			DestroyWindow(State.m_Window);
			return std::nullopt;
		}
		SendMessageW(State.m_Details, WM_SETFONT, reinterpret_cast<WPARAM>(State.m_BodyFont), TRUE);
		SendMessageW(State.m_Details, EM_SETREADONLY, TRUE, 0);
		SendMessageW(State.m_Details, EM_SETSEL, 0, 0);

		State.m_vButtons.reserve(MessageBox.m_vButtons.size());
		for(size_t Index = 0; Index < MessageBox.m_vButtons.size(); ++Index)
		{
			const int ButtonId = gs_ButtonIdBase + static_cast<int>(Index);
			const auto &ButtonDescription = MessageBox.m_vButtons[Index];
			HWND Button = CreateWindowExW(0, L"BUTTON", State.m_vButtonLabels[Index].c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
				0, 0, 1, 1, State.m_Window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ButtonId)), Instance, nullptr);
			if(Button == nullptr)
			{
				DestroyWindow(State.m_Window);
				return std::nullopt;
			}
			SendMessageW(Button, WM_SETFONT, reinterpret_cast<WPARAM>(State.m_ButtonFont), TRUE);
			State.m_vButtons.push_back(Button);
			if(ButtonDescription.m_Confirm)
				State.m_ConfirmButtonId = ButtonId;
			if(ButtonDescription.m_Cancel)
				State.m_CancelButtonId = ButtonId;
		}

		LayoutDialog(State);
		UpdateWindowCornerRegion(State.m_Window, State.m_Dpi);
		PrepareFireworkParticles(State);
		State.m_FireworksFrame = 0;
		SetTimer(State.m_Window, gs_FireworksTimerId, gs_FireworksTimerPeriodMs, nullptr);
		const bool HiddenForTest = FestiveDialogHiddenForTest();
		ShowWindow(State.m_Window, HiddenForTest ? SW_HIDE : SW_SHOWNORMAL);
		// 首次显示后再应用一次，确保 WM_SIZE/工作区裁剪已经给出最终客户区尺寸。
		UpdateWindowCornerRegion(State.m_Window, State.m_Dpi);
		if(!HiddenForTest)
		{
			// 崩溃报告通常由已经退出或失去前台资格的客户端进程启动。
			// 先短暂置顶再恢复普通层级，确保窗口初次出现即可接收鼠标输入。
			SetWindowPos(State.m_Window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			SetWindowPos(State.m_Window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
			SetForegroundWindow(State.m_Window);
			SetActiveWindow(State.m_Window);
		}
		UpdateWindow(State.m_Window);
		StartFestiveMusic();
		if(State.m_ConfirmButtonId >= gs_ButtonIdBase)
			SetFocus(State.m_vButtons[State.m_ConfirmButtonId - gs_ButtonIdBase]);

		MSG Message;
		while(State.m_Running && GetMessageW(&Message, nullptr, 0, 0) > 0)
		{
			if(Message.message == WM_KEYDOWN && Message.wParam == VK_ESCAPE)
			{
				SendMessageW(State.m_Window, WM_CLOSE, 0, 0);
				continue;
			}
			if(Message.message == WM_KEYDOWN && Message.wParam == VK_RETURN && State.m_ConfirmButtonId >= gs_ButtonIdBase)
			{
				SendMessageW(State.m_Window, WM_COMMAND, MAKEWPARAM(State.m_ConfirmButtonId, BN_CLICKED), 0);
				continue;
			}
			if(!IsDialogMessageW(State.m_Window, &Message))
			{
				TranslateMessage(&Message);
				DispatchMessageW(&Message);
			}
		}
		// GetMessageW 返回 0（WM_QUIT）或 -1 时窗口可能仍然存在，而 State 马上析构，
		// 必须销毁窗口，否则 GWLP_USERDATA 会悬垂指向已释放的栈对象。
		if(IsWindow(State.m_Window))
			DestroyWindow(State.m_Window);
		return State.m_Result >= 0 ? std::optional<int>(State.m_Result) : std::nullopt;
	}
	catch(...)
	{
		// 准备失败由状态owner先清理窗口，再交调用方沿原SDL消息框回退。
		return std::nullopt;
	}
}

#else

std::optional<int> ShowQmFestiveMessageBox(const IGraphics::CMessageBox &MessageBox)
{
	(void)MessageBox;
	return std::nullopt;
}

#endif
