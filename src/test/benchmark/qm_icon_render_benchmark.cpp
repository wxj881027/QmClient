// 对照生产图集与生产字体缓存：设备调用只计数，不创建 GPU 命令或测 GPU。
#include <base/system.h>

#include <engine/console.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/qm_icon_font_render.h>
#include <game/client/qm_icon_manager.h>

#include <benchmark/benchmark.h>
#include <test/support/icon_benchmark_graphics.h>

#include <array>
#include <filesystem>
#include <memory>
#include <string>

namespace
{
	constexpr int NUM_ICONS = 24;
	struct SIcon
	{
		EQmIcon m_Id;
		int m_Codepoint;
	};
	const SIcon ICONS[] = {
#define QM_ICON_ENTRY(Id, Name, Codepoint) {EQmIcon::Id, Codepoint},
#define QM_ICON_ALIAS(Id, Name, Codepoint)
#include <game/client/qm_icon_registry.inc>
#undef QM_ICON_ENTRY
#undef QM_ICON_ALIAS
	};

	struct CScopedConfig
	{
		CConfig m_Previous = g_Config;
		~CScopedConfig() { g_Config = m_Previous; }
	};

	// 每个 fixture 自己注册 kernel，并恢复配置；不会使用真实用户目录。
	class CIconRenderFixture
	{
		CScopedConfig m_ConfigGuard;
		std::unique_ptr<IKernel> m_pKernel;
		std::unique_ptr<IConsole> m_pConsole;
		std::unique_ptr<IStorage> m_pStorage;

	public:
		CIconBenchmarkGraphics m_Graphics;
		CQmIconManager m_Icons;
		std::unique_ptr<IEngineTextRender> m_pText;
		std::array<STextContainerIndex, NUM_ICONS> m_aContainers;
		std::array<std::string, NUM_ICONS> m_aGlyphs;
		bool m_Ready = false;
		explicit CIconRenderFixture(bool Atlas) : m_pKernel(IKernel::Create()), m_pConsole(CreateConsole(CFGFLAG_CLIENT))
		{
			g_Config.m_QmPerfDebug = g_Config.m_QmPerfLogfile = g_Config.m_QmPerfStutterDiagnostics = 0;
			g_Config.m_QmUiIconWeight = 0;
			g_Config.m_QmUiIconColor = 0;
			g_Config.m_QmUiIconCustomColorEnabled = 0;
			const char *pArgs[] = {DDNET_TEST_SOURCE_DIR "/QmClient.exe"};
			static uint64_t Sequence = 0;
			const std::string Directory = std::string(DDNET_TEST_SOURCE_DIR "/tmp/icon-render-benchmark/") + std::to_string(time_get_nanoseconds().count()) + "-" + std::to_string(++Sequence);
			if(!std::filesystem::create_directories(Directory))
				return;
			m_pStorage = CreateTempStorage(Directory.c_str(), 1, pArgs);
			if(!m_pStorage)
				return;
			m_Graphics.m_pStorage = m_pStorage.get();
			m_pKernel->RegisterInterface<IConsole>(m_pConsole.get(), false);
			m_pKernel->RegisterInterface<IStorage>(m_pStorage.get(), false);
			m_pKernel->RegisterInterface<IGraphics>(&m_Graphics, false);
			for(int i = 0; i < NUM_ICONS; ++i)
			{
				char aGlyph[5] = {};
				str_utf8_encode(aGlyph, ICONS[i].m_Codepoint);
				m_aGlyphs[i] = aGlyph;
				if(CQmIconManager::IconFromGlyph(aGlyph) != ICONS[i].m_Id)
					return;
			}
			if(Atlas)
			{
				m_Icons.Init(&m_Graphics, m_pStorage.get(), m_pConsole.get());
				m_Ready = m_Icons.Reload();
			}
			else
			{
				m_pText.reset(CreateEngineTextRender());
				m_pKernel->RegisterInterface<IEngineTextRender>(m_pText.get(), false);
				m_pText->Init();
				m_Ready = m_pText->LoadFonts();
				m_pText->SetFontPreset(EFontPreset::ICON_FONT);
			}
		}
		~CIconRenderFixture()
		{
			if(m_pText)
			{
				for(auto &Container : m_aContainers)
					if(Container.Valid())
						m_pText->DeleteTextContainer(Container);
				m_pText->Shutdown();
			}
			m_Icons.Shutdown();
		}
		void PrepareCached(int Size)
		{
			m_pText->TextColor(ColorRGBA(1, 1, 1, 1));
			m_pText->TextOutlineColor(ColorRGBA(1, 1, 1, 1));
			for(int i = 0; i < NUM_ICONS; ++i)
			{
				CTextCursor Cursor;
				Cursor.m_FontSize = Size;
				Cursor.SetPosition(vec2(i * (Size + 4), 0));
				if(!m_pText->CreateTextContainer(m_aContainers[i], &Cursor, m_aGlyphs[i].c_str()) || Cursor.m_GlyphCount != 1)
					throw std::logic_error("font did not prepare one glyph");
			}
		}
		void Draw(int Path, int Size, bool LowContrast)
		{
			CUiScopedSurfaceText Surface(nullptr, LowContrast ? ColorRGBA(1, 1, 1, 1) : ColorRGBA(0, 0, 0, 1));
			const ColorRGBA Fill = ConfiguredQmUiIconColor(ColorRGBA(1, 1, 1, 1));
			const ColorRGBA Outline = ConfiguredQmUiIconContrastColor(Fill);
			for(int i = 0; i < NUM_ICONS; ++i)
			{
				if(Path == 0)
				{
					CUIRect Rect;
					Rect.x = i * (Size + 4);
					Rect.y = 0;
					Rect.w = Rect.h = Size;
					if(!m_Icons.RenderIcon(ICONS[i].m_Id, Rect, ColorRGBA(1, 1, 1, 1)))
						throw std::logic_error("atlas did not draw benchmark icon");
				}
				else if(Path == 1)
					m_pText->RenderTextContainer(m_aContainers[i], Fill, Outline);
				else
				{
					CTextCursor Cursor;
					Cursor.SetPosition(vec2(i * (Size + 4), 0));
					Cursor.m_FontSize = Size;
					QmRenderImmediateFontIcon(*m_pText, &Cursor, m_aGlyphs[i].c_str(), -1, Fill, Outline);
				}
			}
		}
	};

	void BM_IconRenderCpuBoundary(benchmark::State &State)
	{
		try
		{
			const int Path = static_cast<int>(State.range(0));
			const int Size = static_cast<int>(State.range(1));
			const bool LowContrast = State.range(2) != 0;
			CIconRenderFixture Fixture(Path == 0);
			if(!Fixture.m_Ready)
			{
				State.SkipWithError("bundled production resources failed to load");
				return;
			}
			if(Path == 1)
				Fixture.PrepareCached(Size);
			Fixture.Draw(Path, Size, LowContrast);
			if(Fixture.m_Graphics.m_Draws < NUM_ICONS || Fixture.m_Graphics.m_Quads < NUM_ICONS)
			{
				State.SkipWithError("production renderer submitted incomplete icon batch");
				return;
			}
			Fixture.m_Graphics.ResetCounters();
			Fixture.m_Graphics.m_ValidateLifecycle = false;
			for(auto _ : State)
			{
				Fixture.Draw(Path, Size, LowContrast);
				benchmark::ClobberMemory();
			}
			const uint64_t ExpectedDraws = State.iterations() * NUM_ICONS * (Path == 0 && LowContrast ? 2 : 1);
			if(Fixture.m_Graphics.m_Draws != ExpectedDraws || Fixture.m_Graphics.m_Quads != ExpectedDraws || Fixture.m_Graphics.m_Uploads != 0 || Fixture.m_Graphics.m_UploadBytes != 0 || (Path == 1 && Fixture.m_Graphics.m_BufferBytes != 0))
			{
				State.SkipWithError("warm batch draw count or zero-upload invariant failed");
				return;
			}
			const double Batches = static_cast<double>(State.iterations());
			State.counters["device_draws_per_batch"] = Fixture.m_Graphics.m_Draws / Batches;
			State.counters["upload_calls_per_batch"] = Fixture.m_Graphics.m_Uploads / Batches;
			State.counters["upload_bytes_per_batch"] = Fixture.m_Graphics.m_UploadBytes / Batches;
			State.counters["container_bytes_per_batch"] = Fixture.m_Graphics.m_BufferBytes / Batches;
			State.SetItemsProcessed(State.iterations() * NUM_ICONS);
			State.SetLabel(Path == 0 ? "mtsdf;warm;cpu-before-device;no-gpu" : Path == 1 ? "ttf-cached-container;warm;cpu-before-device;no-gpu" :
												       "ttf-immediate;warm;cpu-before-device;no-gpu");
		}
		catch(const std::exception &Error)
		{
			State.SkipWithError(Error.what());
		}
	}
	BENCHMARK(BM_IconRenderCpuBoundary)->ArgsProduct({{0, 1, 2}, {16, 24, 36}, {0, 1}});

	// 首批字形准备：全新生产 renderer 的初始化与字体文件加载不计时。
	// 固定批次数，避免完整字体 fixture 准备把自适应迭代拖成长时间运行。
	void BM_IconFreshGlyphsCpuBoundary(benchmark::State &State)
	{
		const int Size = static_cast<int>(State.range(0));
		uint64_t Uploads = 0, UploadBytes = 0;
		for(auto _ : State)
		{
			State.PauseTiming();
			std::unique_ptr<CIconRenderFixture> pFixture;
			try
			{
				pFixture = std::make_unique<CIconRenderFixture>(false);
				if(!pFixture->m_Ready)
					throw std::logic_error("fresh bundled font fixture failed");
				pFixture->m_Graphics.ResetCounters();
				pFixture->m_Graphics.m_ValidateLifecycle = false;
			}
			catch(const std::exception &Error)
			{
				pFixture.reset();
				State.ResumeTiming();
				State.SkipWithError(Error.what());
				break;
			}
			State.ResumeTiming();
			try
			{
				pFixture->Draw(2, Size, false);
				benchmark::ClobberMemory();
			}
			catch(const std::exception &Error)
			{
				State.PauseTiming();
				pFixture.reset();
				State.ResumeTiming();
				State.SkipWithError(Error.what());
				break;
			}
			State.PauseTiming();
			const bool Valid = pFixture->m_Graphics.m_Draws == NUM_ICONS && pFixture->m_Graphics.m_Quads == NUM_ICONS && pFixture->m_Graphics.m_Uploads > 0;
			Uploads += pFixture->m_Graphics.m_Uploads;
			UploadBytes += pFixture->m_Graphics.m_UploadBytes;
			pFixture.reset();
			State.ResumeTiming();
			if(!Valid)
			{
				State.SkipWithError("fresh glyph batch failed draw/upload invariant");
				break;
			}
		}
		if(!State.skipped())
		{
			State.counters["upload_calls_per_batch"] = static_cast<double>(Uploads) / State.iterations();
			State.counters["upload_bytes_per_batch"] = static_cast<double>(UploadBytes) / State.iterations();
			State.SetItemsProcessed(State.iterations() * NUM_ICONS);
		}
		State.SetLabel("ttf-fresh24-glyphs;init-font-load-excluded;cpu-before-device;no-gpu");
	}
	// 固定短批次的 CPU 累计值可能被 Windows 计时精度舍入为零，速率按实际经过时间计算。
	BENCHMARK(BM_IconFreshGlyphsCpuBoundary)->Arg(16)->Arg(24)->Arg(36)->Iterations(16)->UseRealTime();

	// 生产图集重载包含 manifest/PNG 文件读取、解析、解码与 CPU 资源替换；OS 缓存已预热。
	void BM_IconAtlasReloadCpuBoundary(benchmark::State &State)
	{
		try
		{
			CIconRenderFixture Fixture(true);
			if(!Fixture.m_Ready)
			{
				State.SkipWithError("atlas fixture failed");
				return;
			}
			Fixture.m_Graphics.ResetCounters();
			Fixture.m_Graphics.m_ValidateLifecycle = false;
			for(auto _ : State)
			{
				if(!Fixture.m_Icons.Reload())
				{
					State.SkipWithError("production atlas reload failed");
					break;
				}
				benchmark::ClobberMemory();
			}
			if(!State.skipped())
			{
				if(Fixture.m_Graphics.m_Uploads != static_cast<uint64_t>(State.iterations()) || Fixture.m_Graphics.m_UploadBytes == 0)
				{
					State.SkipWithError("atlas reload did not decode/upload each iteration");
					return;
				}
				State.counters["upload_bytes_per_reload"] = static_cast<double>(Fixture.m_Graphics.m_UploadBytes) / State.iterations();
				State.SetItemsProcessed(State.iterations());
			}
			State.SetLabel("mtsdf-regular-reload;os-cache-warm;io-json-png;device-upload-excluded");
		}
		catch(const std::exception &Error)
		{
			State.SkipWithError(Error.what());
		}
	}
	BENCHMARK(BM_IconAtlasReloadCpuBoundary)->Iterations(16);

	// 每批在三个已预热字号间切换；新字号首次建立成本由 FreshGlyphs 单独测量。
	void BM_IconWarmSizeSwitchCpuBoundary(benchmark::State &State)
	{
		try
		{
			const int Path = static_cast<int>(State.range(0));
			CIconRenderFixture Fixture(Path == 0);
			if(!Fixture.m_Ready)
			{
				State.SkipWithError("size-switch fixture failed");
				return;
			}
			for(int Size : {16, 24, 36})
				Fixture.Draw(Path == 0 ? 0 : 2, Size, false);
			Fixture.m_Graphics.ResetCounters();
			Fixture.m_Graphics.m_ValidateLifecycle = false;
			int Index = 0;
			const int Sizes[] = {16, 24, 36};
			for(auto _ : State)
			{
				Fixture.Draw(Path == 0 ? 0 : 2, Sizes[Index++ % 3], false);
				benchmark::ClobberMemory();
			}
			if(Fixture.m_Graphics.m_Draws != static_cast<uint64_t>(State.iterations()) * NUM_ICONS || Fixture.m_Graphics.m_Uploads != 0)
			{
				State.SkipWithError("warm size-switch count/upload invariant failed");
				return;
			}
			State.SetItemsProcessed(State.iterations() * NUM_ICONS);
			State.SetLabel(Path == 0 ? "mtsdf;warm16-24-36-switch;cpu-before-device;no-gpu" : "ttf-immediate;warm16-24-36-switch;cpu-before-device;no-gpu");
		}
		catch(const std::exception &Error)
		{
			State.SkipWithError(Error.what());
		}
	}
	BENCHMARK(BM_IconWarmSizeSwitchCpuBoundary)->Arg(0)->Arg(1);
} // namespace
